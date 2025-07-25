// KairosServer/include/Core/CommandProcessor.hpp
#pragma once

#include <Protocol.hpp>
#include <Graphics/RenderCommand.hpp>
#include <memory>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <vector>

namespace Kairos {

class RaylibRenderer;
class LayerManager;
class FontManager;

/**
 * @brief Processes network messages and converts them to render commands
 */
class CommandProcessor {
public:
    struct Stats {
        std::atomic<uint64_t> commands_received{0};
        std::atomic<uint64_t> commands_processed{0};
        std::atomic<uint64_t> commands_dropped{0};
        std::atomic<uint32_t> invalid_commands{0};
        std::atomic<uint32_t> processing_errors{0};
        
        std::atomic<uint32_t> queue_size{0};
        std::atomic<uint32_t> commands_per_second{0};
        double avg_processing_time_us = 0.0;
    };

public:
    CommandProcessor(RaylibRenderer& renderer, LayerManager& layer_manager, FontManager& font_manager);
    ~CommandProcessor();
    
    // Lifecycle
    bool initialize();
    void shutdown();
    
    // Message processing
    bool processNetworkMessage(const MessageHeader& header, const std::vector<uint8_t>& data);
    void processCommandBatch(const std::vector<RenderCommand>& commands);
    void processCommand(const RenderCommand& command);
    
    // Statistics
    Stats getStats() const;
    void resetStats();

private:
    // Internal processing
    void processLayerCommands(uint8_t layer_id, const std::vector<const RenderCommand*>& commands);
    void processBatchedTexturedQuads(uint8_t layer_id, const std::vector<const RenderCommand*>& commands);
    void processBatchedText(uint8_t layer_id, const std::vector<const RenderCommand*>& commands);
    
    // Threading
    void processingLoop();
    void updateStatistics();
    
private:
    RaylibRenderer& m_renderer;
    LayerManager& m_layer_manager;
    FontManager& m_font_manager;
    
    std::unique_ptr<RenderCommandQueue> m_command_queue;
    std::thread m_processing_thread;
    std::atomic<bool> m_stop_processing{false};
    
    Stats m_stats;
};

/**
 * @brief Convert network protocol commands to internal render commands
 */
class CommandConverter {
public:
    static RenderCommand fromNetworkMessage(const MessageHeader& header, const void* data);
    static std::vector<RenderCommand> fromNetworkBatch(const std::vector<uint8_t>& buffer);
    
    // Convert specific command types
    static RenderCommand fromDrawPointData(const DrawPointData& data, uint8_t layer_id);
    static RenderCommand fromDrawLineData(const DrawLineData& data, uint8_t layer_id);
    static RenderCommand fromDrawRectangleData(const DrawRectangleData& data, uint8_t layer_id, bool filled);
    static RenderCommand fromDrawTextData(const DrawTextData& data, const std::string& text, uint8_t layer_id);
    static RenderCommand fromDrawTexturedQuadsData(const DrawTexturedQuadsData& data, 
                                                  const std::vector<TexturedVertex>& vertices, uint8_t layer_id);
    
    // Priority assignment based on command type and context
    static RenderCommand::Priority assignPriority(MessageType message_type, uint8_t layer_id);
};

} // namespace Kairos