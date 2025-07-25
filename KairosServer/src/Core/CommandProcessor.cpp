// KairosServer/src/Core/CommandProcessor.cpp
#include "CommandProcessor.hpp"
#include "RaylibRenderer.hpp"
#include "LayerManager.hpp"
#include "FontManager.hpp"
#include "Utils/Logger.hpp"
#include <cstring>
#include <algorithm>

namespace Kairos {

CommandProcessor::CommandProcessor(RaylibRenderer& renderer, LayerManager& layer_manager, FontManager& font_manager)
    : m_renderer(renderer), m_layer_manager(layer_manager), m_font_manager(font_manager) {
    
    m_command_queue = std::make_unique<RenderCommandQueue>(10000);
    
    Logger::info("CommandProcessor initialized");
}

CommandProcessor::~CommandProcessor() {
    if (m_processing_thread.joinable()) {
        m_stop_processing = true;
        m_processing_thread.join();
    }
}

bool CommandProcessor::initialize() {
    m_stop_processing = false;
    m_processing_thread = std::thread(&CommandProcessor::processingLoop, this);
    
    Logger::info("CommandProcessor started processing thread");
    return true;
}

void CommandProcessor::shutdown() {
    m_stop_processing = true;
    
    if (m_processing_thread.joinable()) {
        m_processing_thread.join();
    }
    
    // Clear any remaining commands
    m_command_queue->clear();
    
    Logger::info("CommandProcessor shutdown complete");
}

bool CommandProcessor::processNetworkMessage(const MessageHeader& header, const std::vector<uint8_t>& data) {
    // Convert network message to internal render command
    RenderCommand command = CommandConverter::fromNetworkMessage(header, data.data());
    
    if (command.type == RenderCommand::Type::DRAW_POINT && command.point.position.x < 0) {
        Logger::warning("Received invalid command from client {}", header.client_id);
        return false;
    }
    
    // Set metadata
    command.client_id = header.client_id;
    command.sequence_id = header.sequence;
    command.timestamp = header.timestamp;
    command.layer_id = header.layer_id;
    
    // Assign priority based on command type and layer
    command.priority = CommandConverter::assignPriority(header.type, header.layer_id);
    
    // Queue command for processing
    bool success = m_command_queue->enqueue(std::move(command));
    
    if (success) {
        m_stats.commands_received.fetch_add(1);
    } else {
        m_stats.commands_dropped.fetch_add(1);
        Logger::warning("Command queue full, dropped command from client {}", header.client_id);
    }
    
    return success;
}

void CommandProcessor::processCommandBatch(const std::vector<RenderCommand>& commands) {
    if (commands.empty()) {
        return;
    }
    
    auto start_time = std::chrono::steady_clock::now();
    
    // Group commands by type and layer for optimal processing
    std::unordered_map<uint8_t, std::vector<const RenderCommand*>> commands_by_layer;
    std::vector<const RenderCommand*> high_priority_commands;
    
    for (const auto& command : commands) {
        if (command.priority >= RenderCommand::Priority::HIGH) {
            high_priority_commands.push_back(&command);
        } else {
            commands_by_layer[command.layer_id].push_back(&command);
        }
    }
    
    // Process high priority commands first
    for (const auto* command : high_priority_commands) {
        processCommand(*command);
    }
    
    // Process regular commands by layer
    for (auto& [layer_id, layer_commands] : commands_by_layer) {
        processLayerCommands(layer_id, layer_commands);
    }
    
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    
    m_stats.commands_processed.fetch_add(commands.size());
    m_stats.avg_processing_time_us = (m_stats.avg_processing_time_us * 0.9) + (duration.count() * 0.1);
    
    Logger::debug("Processed {} commands in {} μs", commands.size(), duration.count());
}

void CommandProcessor::processCommand(const RenderCommand& command) {
    switch (command.type) {
        case RenderCommand::Type::DRAW_POINT:
            m_renderer.drawPoint(command.point.position, command.point.color, command.layer_id);
            break;
            
        case RenderCommand::Type::DRAW_LINE:
            m_renderer.drawLine(command.line.start, command.line.end, 
                              command.line.color, command.line.thickness, command.layer_id);
            break;
            
        case RenderCommand::Type::DRAW_RECTANGLE:
            m_renderer.drawRectangle(command.rectangle.position, 
                                   command.rectangle.width, command.rectangle.height,
                                   command.rectangle.color, command.rectangle.filled, 
                                   command.layer_id);
            break;
            
        case RenderCommand::Type::DRAW_CIRCLE:
            m_renderer.drawCircle(command.circle.center, command.circle.radius,
                                command.circle.color, command.circle.filled, command.layer_id);
            break;
            
        case RenderCommand::Type::DRAW_TEXT:
            if (!command.text_string.empty()) {
                m_renderer.drawText(command.text_string, command.text.position,
                                  command.text.font_id, command.text.font_size,
                                  command.text.color, command.layer_id);
            }
            break;
            
        case RenderCommand::Type::DRAW_TEXTURED_QUADS:
            if (!command.vertices.empty()) {
                m_renderer.drawTexturedQuads(command.vertices, 
                                           command.textured_quads.texture_id, 
                                           command.layer_id);
            }
            break;
            
        case RenderCommand::Type::CLEAR_LAYER:
            m_renderer.clearLayer(command.layer_id);
            m_layer_manager.markLayerDirty(command.layer_id);
            break;
            
        case RenderCommand::Type::SET_LAYER_VISIBILITY:
            m_renderer.setLayerVisibility(command.layer_id, command.layer_visibility.visible);
            m_layer_manager.setLayerVisibility(command.layer_id, command.layer_visibility.visible);
            break;
            
        case RenderCommand::Type::SET_VIEWPORT:
            m_renderer.setViewport(command.viewport.x, command.viewport.y,
                                 command.viewport.width, command.viewport.height);
            break;
            
        case RenderCommand::Type::SET_CAMERA:
            {
                Vector2 target = {command.camera.target.x, command.camera.target.y};
                Vector2 offset = {command.camera.offset.x, command.camera.offset.y};
                m_renderer.setCamera2D(target, offset, command.camera.rotation, command.camera.zoom);
            }
            break;
            
        default:
            Logger::warning("Unknown render command type: {}", static_cast<int>(command.type));
            m_stats.invalid_commands.fetch_add(1);
            break;
    }
}

void CommandProcessor::processLayerCommands(uint8_t layer_id, 
                                           const std::vector<const RenderCommand*>& commands) {
    if (commands.empty()) {
        return;
    }
    
    // Mark layer as dirty for this frame
    m_layer_manager.markLayerDirty(layer_id);
    
    // Group commands by type for potential batching
    std::vector<const RenderCommand*> text_commands;
    std::vector<const RenderCommand*> textured_commands;
    std::vector<const RenderCommand*> primitive_commands;
    
    for (const auto* command : commands) {
        switch (command->type) {
            case RenderCommand::Type::DRAW_TEXT:
                text_commands.push_back(command);
                break;
            case RenderCommand::Type::DRAW_TEXTURED_QUADS:
                textured_commands.push_back(command);
                break;
            default:
                primitive_commands.push_back(command);
                break;
        }
    }
    
    // Process batched commands
    if (!textured_commands.empty()) {
        processBatchedTexturedQuads(layer_id, textured_commands);
    }
    
    if (!text_commands.empty()) {
        processBatchedText(layer_id, text_commands);
    }
    
    // Process primitive commands individually
    for (const auto* command : primitive_commands) {
        processCommand(*command);
    }
}

void CommandProcessor::processBatchedTexturedQuads(uint8_t layer_id, 
                                                  const std::vector<const RenderCommand*>& commands) {
    // Group by texture for optimal batching
    std::unordered_map<uint32_t, std::vector<TexturedVertex>> vertices_by_texture;
    
    for (const auto* command : commands) {
        uint32_t texture_id = command->textured_quads.texture_id;
        auto& vertices = vertices_by_texture[texture_id];
        vertices.insert(vertices.end(), command->vertices.begin(), command->vertices.end());
    }
    
    // Render each texture batch
    for (const auto& [texture_id, vertices] : vertices_by_texture) {
        m_renderer.drawTexturedQuads(vertices, texture_id, layer_id);
    }
    
    Logger::debug("Batched {} textured quad commands into {} texture draws", 
                 commands.size(), vertices_by_texture.size());
}

void CommandProcessor::processBatchedText(uint8_t layer_id, 
                                        const std::vector<const RenderCommand*>& commands) {
    // Group by font for optimal batching
    std::unordered_map<uint32_t, std::vector<const RenderCommand*>> commands_by_font;
    
    for (const auto* command : commands) {
        commands_by_font[command->text.font_id].push_back(command);
    }
    
    // Process each font group
    for (const auto& [font_id, font_commands] : commands_by_font) {
        for (const auto* command : font_commands) {
            processCommand(*command);
        }
    }
    
    Logger::debug("Batched {} text commands by {} fonts", 
                 commands.size(), commands_by_font.size());
}

void CommandProcessor::processingLoop() {
    Logger::info("Command processing loop started");
    
    while (!m_stop_processing) {
        try {
            // Dequeue commands in batches for optimal processing
            auto commands = m_command_queue->dequeueBatch(1000);
            
            if (!commands.empty()) {
                processCommandBatch(commands);
            } else {
                // No commands to process, sleep briefly
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            
            // Update statistics periodically
            updateStatistics();
            
        } catch (const std::exception& e) {
            Logger::error("Exception in command processing loop: {}", e.what());
            m_stats.processing_errors.fetch_add(1);
        }
    }
    
    Logger::info("Command processing loop stopped");
}

void CommandProcessor::updateStatistics() {
    static auto last_update = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    
    if (now - last_update >= std::chrono::seconds(1)) {
        m_stats.queue_size = m_command_queue->size();
        
        // Calculate commands per second
        static uint64_t last_processed = 0;
        uint64_t current_processed = m_stats.commands_processed.load();
        m_stats.commands_per_second = current_processed - last_processed;
        last_processed = current_processed;
        
        last_update = now;
        
        if (Logger::getLevel() <= Logger::Level::Debug) {
            Logger::debug("CommandProcessor stats: queue={}, processed={}/s, avg_time={}μs",
                         m_stats.queue_size, m_stats.commands_per_second, 
                         m_stats.avg_processing_time_us);
        }
    }
}

CommandProcessor::Stats CommandProcessor::getStats() const {
    Stats stats = m_stats;
    stats.queue_size = m_command_queue->size();
    return stats;
}

void CommandProcessor::resetStats() {
    m_stats = Stats{};
    Logger::debug("CommandProcessor statistics reset");
}

// CommandConverter implementation

RenderCommand CommandConverter::fromNetworkMessage(const MessageHeader& header, const void* data) {
    switch (header.type) {
        case MessageType::DRAW_POINT: {
            const auto* point_data = static_cast<const DrawPointData*>(data);
            return fromDrawPointData(*point_data, header.layer_id);
        }
        
        case MessageType::DRAW_LINE: {
            const auto* line_data = static_cast<const DrawLineData*>(data);
            return fromDrawLineData(*line_data, header.layer_id);
        }
        
        case MessageType::DRAW_RECTANGLE: {
            const auto* rect_data = static_cast<const DrawRectangleData*>(data);
            return fromDrawRectangleData(*rect_data, header.layer_id, false);
        }
        
        case MessageType::FILL_RECTANGLE: {
            const auto* rect_data = static_cast<const DrawRectangleData*>(data);
            return fromDrawRectangleData(*rect_data, header.layer_id, true);
        }
        
        case MessageType::DRAW_TEXT: {
            const auto* text_data = static_cast<const DrawTextData*>(data);
            std::string text(static_cast<const char*>(data) + sizeof(DrawTextData), text_data->text_length);
            return fromDrawTextData(*text_data, text, header.layer_id);
        }
        
        case MessageType::DRAW_TEXTURED_QUADS: {
            const auto* quad_data = static_cast<const DrawTexturedQuadsData*>(data);
            const auto* vertices = reinterpret_cast<const TexturedVertex*>(
                static_cast<const uint8_t*>(data) + sizeof(DrawTexturedQuadsData));
            std::vector<TexturedVertex> vertex_vector(vertices, vertices + (quad_data->quad_count * 4));
            return fromDrawTexturedQuadsData(*quad_data, vertex_vector, header.layer_id);
        }
        
        case MessageType::CLEAR_LAYER: {
            RenderCommand command(RenderCommand::Type::CLEAR_LAYER, header.layer_id);
            return command;
        }
        
        default: {
            Logger::warning("Unknown message type for conversion: {}", static_cast<int>(header.type));
            return RenderCommand{}; // Default empty command
        }
    }
}

RenderCommand CommandConverter::fromDrawPointData(const DrawPointData& data, uint8_t layer_id) {
    RenderCommand command(RenderCommand::Type::DRAW_POINT, layer_id);
    command.point.position = data.position;
    command.point.color = Color{255, 255, 255, 255}; // Default white
    command.estimated_vertex_count = 1;
    return command;
}

RenderCommand CommandConverter::fromDrawLineData(const DrawLineData& data, uint8_t layer_id) {
    RenderCommand command(RenderCommand::Type::DRAW_LINE, layer_id);
    command.line.start = data.start;
    command.line.end = data.end;
    command.line.color = Color{255, 255, 255, 255}; // Default white
    command.line.thickness = 1.0f;
    command.estimated_vertex_count = 2;
    return command;
}

RenderCommand CommandConverter::fromDrawRectangleData(const DrawRectangleData& data, 
                                                     uint8_t layer_id, bool filled) {
    RenderCommand command(RenderCommand::Type::DRAW_RECTANGLE, layer_id);
    command.rectangle.position = data.position;
    command.rectangle.width = data.width;
    command.rectangle.height = data.height;
    command.rectangle.color = Color{255, 255, 255, 255}; // Default white
    command.rectangle.filled = filled;
    command.estimated_vertex_count = filled ? 4 : 8; // 4 for filled, 8 for outline
    return command;
}

RenderCommand CommandConverter::fromDrawTextData(const DrawTextData& data, 
                                                const std::string& text, uint8_t layer_id) {
    RenderCommand command(RenderCommand::Type::DRAW_TEXT, layer_id);
    command.text.position = data.position;
    command.text.font_id = data.font_id;
    command.text.font_size = data.font_size;
    command.text.color = Color{255, 255, 255, 255}; // Default white
    command.text_string = text;
    command.estimated_vertex_count = text.length() * 6; // 6 vertices per character (2 triangles)
    command.estimated_memory_usage = sizeof(RenderCommand) + text.length();
    return command;
}

RenderCommand CommandConverter::fromDrawTexturedQuadsData(const DrawTexturedQuadsData& data,
                                                         const std::vector<TexturedVertex>& vertices, 
                                                         uint8_t layer_id) {
    RenderCommand command(RenderCommand::Type::DRAW_TEXTURED_QUADS, layer_id);
    command.textured_quads.texture_id = data.texture_id;
    command.vertices = vertices;
    command.estimated_vertex_count = vertices.size();
    command.estimated_memory_usage = sizeof(RenderCommand) + (vertices.size() * sizeof(TexturedVertex));
    return command;
}

RenderCommand::Priority CommandConverter::assignPriority(MessageType message_type, uint8_t layer_id) {
    // Layer 0 is typically high priority (UI/HUD)
    if (layer_id == 0) {
        return RenderCommand::Priority::HIGH;
    }
    
    // Critical system messages
    switch (message_type) {
        case MessageType::CLEAR_LAYER:
        case MessageType::CLEAR_ALL_LAYERS:
            return RenderCommand::Priority::HIGH;
            
        case MessageType::DRAW_TEXT:
        case MessageType::DRAW_TEXTURED_QUADS:
            return RenderCommand::Priority::NORMAL;
            
        default:
            return RenderCommand::Priority::NORMAL;
    }
}

} // namespace Kairos