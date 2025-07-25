// KairosServer/include/Graphics/RenderCommand.hpp
#pragma once

#include <Protocol.hpp>
#include <Types.hpp>
#include <vector>
#include <string>
#include <memory>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <queue>

namespace Kairos {

/**
 * @brief Internal render command structure for the server
 * 
 * This is different from the network protocol structures - it's optimized
 * for internal processing and batching by the renderer.
 */
struct RenderCommand {
    enum class Type : uint8_t {
        DRAW_POINT,
        DRAW_LINE,
        DRAW_RECTANGLE,
        DRAW_CIRCLE,
        DRAW_POLYGON,
        DRAW_TEXT,
        DRAW_TEXTURED_QUADS,
        CLEAR_LAYER,
        SET_LAYER_VISIBILITY,
        SET_VIEWPORT,
        SET_CAMERA,
        BATCH_MARKER
    };
    
    enum class Priority : uint8_t {
        LOW = 0,
        NORMAL = 1,
        HIGH = 2,
        CRITICAL = 3
    };
    
    Type type;
    Priority priority = Priority::NORMAL;
    uint8_t layer_id = 0;
    uint32_t client_id = 0;
    uint32_t sequence_id = 0;
    uint64_t timestamp = 0;
    
    // Command-specific data (union for memory efficiency)
    union {
        // Point drawing
        struct {
            Point position;
            Color color;
        } point;
        
        // Line drawing
        struct {
            Point start;
            Point end;
            Color color;
            float thickness;
        } line;
        
        // Rectangle drawing
        struct {
            Point position;
            float width;
            float height;
            Color color;
            bool filled;
        } rectangle;
        
        // Circle drawing
        struct {
            Point center;
            float radius;
            Color color;
            bool filled;
        } circle;
        
        // Text drawing
        struct {
            Point position;
            uint32_t font_id;
            float font_size;
            Color color;
            // Text string stored separately to avoid size issues
        } text;
        
        // Textured quad rendering
        struct {
            uint32_t texture_id;
            // Vertices stored separately
        } textured_quads;
        
        // Layer operations
        struct {
            bool visible;
        } layer_visibility;
        
        // Viewport setting
        struct {
            int x, y;
            int width, height;
        } viewport;
        
        // Camera setting
        struct {
            Point target;
            Point offset;
            float rotation;
            float zoom;
        } camera;
    };
    
    // Additional data for complex commands
    std::string text_string;                    // For text commands
    std::vector<Point> polygon_points;          // For polygon commands
    std::vector<TexturedVertex> vertices;       // For textured quad commands
    
    // Metadata
    std::chrono::steady_clock::time_point created_time;
    size_t estimated_vertex_count = 0;
    size_t estimated_memory_usage = 0;
    
    // Constructors
    RenderCommand() : type(Type::DRAW_POINT), priority(Priority::NORMAL) {
        created_time = std::chrono::steady_clock::now();
    }
    
    explicit RenderCommand(Type cmd_type, uint8_t layer = 0, Priority prio = Priority::NORMAL) 
        : type(cmd_type), priority(prio), layer_id(layer) {
        created_time = std::chrono::steady_clock::now();
    }
    
    // Factory methods for creating specific commands
    static RenderCommand createDrawPoint(const Point& pos, const Color& color, 
                                        uint8_t layer_id = 0, Priority priority = Priority::NORMAL);
    static RenderCommand createDrawLine(const Point& start, const Point& end, 
                                       const Color& color, float thickness = 1.0f,
                                       uint8_t layer_id = 0, Priority priority = Priority::NORMAL);
    static RenderCommand createDrawRectangle(const Point& pos, float width, float height,
                                           const Color& color, bool filled = true,
                                           uint8_t layer_id = 0, Priority priority = Priority::NORMAL);
    static RenderCommand createDrawCircle(const Point& center, float radius,
                                         const Color& color, bool filled = true,
                                         uint8_t layer_id = 0, Priority priority = Priority::NORMAL);
    static RenderCommand createDrawText(const Point& pos, const std::string& text,
                                       uint32_t font_id, float font_size, const Color& color,
                                       uint8_t layer_id = 0, Priority priority = Priority::NORMAL);
    static RenderCommand createDrawTexturedQuads(const std::vector<TexturedVertex>& vertices,
                                                uint32_t texture_id,
                                                uint8_t layer_id = 0, Priority priority = Priority::NORMAL);
    static RenderCommand createClearLayer(uint8_t layer_id);
    static RenderCommand createSetLayerVisibility(uint8_t layer_id, bool visible);
    
    // Utility methods
    bool isDrawingCommand() const;
    bool isLayerCommand() const;
    bool isSystemCommand() const;
    size_t getEstimatedCost() const;
    std::string toString() const;
    
    // Comparison operators for priority queues
    bool operator<(const RenderCommand& other) const;
    bool operator>(const RenderCommand& other) const;
};

/**
 * @brief Batch of render commands for efficient processing
 */
struct RenderCommandBatch {
    std::vector<RenderCommand> commands;
    uint8_t primary_layer_id = 0;
    uint32_t primary_client_id = 0;
    RenderCommand::Priority max_priority = RenderCommand::Priority::LOW;
    
    size_t total_vertex_count = 0;
    size_t total_memory_usage = 0;
    std::chrono::steady_clock::time_point created_time;
    
    RenderCommandBatch() {
        created_time = std::chrono::steady_clock::now();
        commands.reserve(1000); // Reasonable default
    }
    
    void addCommand(RenderCommand&& command);
    void addCommand(const RenderCommand& command);
    void clear();
    bool isEmpty() const { return commands.empty(); }
    size_t size() const { return commands.size(); }
    
    // Sort commands for optimal rendering order
    void optimize();
    
    // Statistics
    size_t getDrawingCommandCount() const;
    size_t getLayerCommandCount() const;
    std::vector<uint8_t> getAffectedLayers() const;
};

/**
 * @brief Command queue with priority support
 */
class RenderCommandQueue {
public:
    explicit RenderCommandQueue(size_t max_size = 10000);
    ~RenderCommandQueue() = default;
    
    // Thread-safe command enqueuing
    bool enqueue(RenderCommand&& command);
    bool enqueue(const RenderCommand& command);
    bool enqueueBatch(RenderCommandBatch&& batch);
    
    // Dequeue commands (single consumer)
    bool dequeue(RenderCommand& command);
    std::vector<RenderCommand> dequeueBatch(size_t max_count = 100);
    RenderCommandBatch dequeueOptimizedBatch(size_t max_count = 100);
    
    // Queue management
    void clear();
    size_t size() const;
    bool empty() const;
    bool full() const;
    
    // Priority-based operations
    void setPriorityThreshold(RenderCommand::Priority threshold);
    size_t getHighPriorityCount() const;
    std::vector<RenderCommand> dequeueHighPriority();
    
    // Statistics
    struct Stats {
        size_t total_enqueued = 0;
        size_t total_dequeued = 0;
        size_t total_dropped = 0;
        size_t current_size = 0;
        size_t peak_size = 0;
        double avg_wait_time_ms = 0.0;
    };
    
    Stats getStats() const;
    void resetStats();

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_condition;
    
    std::vector<RenderCommand> m_commands;
    size_t m_max_size;
    size_t m_head = 0;
    size_t m_tail = 0;
    size_t m_count = 0;
    
    RenderCommand::Priority m_priority_threshold = RenderCommand::Priority::NORMAL;
    
    Stats m_stats;
    
    // Helper methods
    size_t nextIndex(size_t index) const;
    bool hasSpaceForCommand() const;
    void updateStats();
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