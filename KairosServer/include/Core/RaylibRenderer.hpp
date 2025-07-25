// KairosServer/src/Core/RaylibRenderer.hpp
#pragma once

#include <raylib.h>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <atomic>

#include "KairosShared/Protocol.hpp"
#include "Graphics/RenderCommand.hpp"
#include "Graphics/BatchRenderer.hpp"
#include "Utils/Logger.hpp"

namespace Kairos {

class FontManager;
class LayerManager;

/**
 * @brief High-performance Raylib-based renderer
 * 
 * Features:
 * - Automatic batching by texture/color
 * - Layer-based compositing
 * - Font atlas management
 * - Modern OpenGL pipeline through Raylib
 * - Cross-platform rendering
 */
class RaylibRenderer {
public:
    struct Config {
        uint32_t window_width = 1920;
        uint32_t window_height = 1080;
        uint32_t target_fps = 60;
        bool enable_vsync = true;
        bool enable_antialiasing = true;
        uint32_t msaa_samples = 4;
        bool fullscreen = false;
        bool hidden = false;          // For headless mode
        std::string window_title = "Kairos Graphics Server";
        
        // Performance settings
        uint32_t max_batch_size = 10000;
        uint32_t vertex_buffer_size = 1024 * 1024;  // 1MB default
        uint32_t texture_atlas_size = 2048;         // 2048x2048 atlas
        
        // Layer settings
        uint32_t max_layers = 255;
        bool layer_caching = true;
    };

    struct Stats {
        uint64_t frames_rendered = 0;
        uint64_t commands_processed = 0;
        uint64_t vertices_rendered = 0;
        uint64_t draw_calls_issued = 0;
        uint64_t textures_uploaded = 0;
        
        float current_fps = 0.0f;
        float avg_frame_time_ms = 0.0f;
        float avg_cpu_usage = 0.0f;
        
        uint32_t active_layers = 0;
        uint32_t cached_layers = 0;
        uint32_t memory_usage_mb = 0;
        
        std::atomic<uint32_t> queued_commands{0};
        std::atomic<uint32_t> batched_draws{0};
    };

public:
    explicit RaylibRenderer(const Config& config = Config{});
    ~RaylibRenderer();

    // Lifecycle
    bool initialize();
    void shutdown();
    bool isInitialized() const { return m_initialized; }

    // Rendering
    void beginFrame();
    void endFrame();
    bool shouldClose() const;

    // Command processing
    void processCommand(const RenderCommand& command);
    void processCommands(const std::vector<RenderCommand>& commands);
    void flushBatches();

    // Layer management
    void clearLayer(uint8_t layer_id);
    void clearAllLayers();
    void setLayerVisibility(uint8_t layer_id, bool visible);
    void setLayerBlendMode(uint8_t layer_id, int blend_mode);

    // Resource management
    uint32_t uploadTexture(uint32_t texture_id, uint32_t width, uint32_t height,
                          uint32_t format, const void* pixel_data, uint32_t data_size);
    bool deleteTexture(uint32_t texture_id);
    Texture2D* getTexture(uint32_t texture_id);

    // Font management
    uint32_t loadFont(const std::string& font_path, uint32_t font_size);
    Font* getFont(uint32_t font_id);
    bool deleteFont(uint32_t font_id);

    // Drawing primitives (batched)
    void drawPoint(const Point& position, const Color& color, uint8_t layer_id = 0);
    void drawLine(const Point& start, const Point& end, const Color& color, 
                  float thickness = 1.0f, uint8_t layer_id = 0);
    void drawRectangle(const Point& position, float width, float height,
                      const Color& color, bool filled = true, uint8_t layer_id = 0);
    void drawCircle(const Point& center, float radius, const Color& color,
                   bool filled = true, uint8_t layer_id = 0);
    void drawText(const std::string& text, const Point& position, uint32_t font_id,
                 float font_size, const Color& color, uint8_t layer_id = 0);
    void drawTexturedQuads(const std::vector<TexturedVertex>& vertices, uint32_t texture_id,
                          uint8_t layer_id = 0);

    // Viewport and transforms
    void setViewport(int x, int y, int width, int height);
    void setCamera2D(const Vector2& target, const Vector2& offset, float rotation, float zoom);
    void resetCamera2D();

    // Performance and debugging
    const Stats& getStats() const { return m_stats; }
    void resetStats();
    
    // Configuration
    void setConfig(const Config& config);
    const Config& getConfig() const { return m_config; }
    
    // Events
    void handleWindowResize(int width, int height);

private:
    // Internal rendering methods
    void renderLayers();
    void renderLayer(uint8_t layer_id);
    void optimizeBatches();
    void updateStats();

    // Resource management internals
    void initializeDefaultResources();
    void cleanupResources();
    uint32_t generateResourceId();

    // Batch management
    struct BatchGroup {
        uint32_t texture_id = 0;
        Color tint_color = {255, 255, 255, 255};
        std::vector<TexturedVertex> vertices;
        uint8_t layer_id = 0;
        bool needs_flush = false;
        
        void clear() {
            vertices.clear();
            needs_flush = false;
        }
        
        bool isEmpty() const {
            return vertices.empty();
        }
    };

    void addToBatch(uint32_t texture_id, const std::vector<TexturedVertex>& vertices,
                   const Color& tint, uint8_t layer_id);
    void flushBatch(BatchGroup& batch);

    // Layer rendering with caching
    struct LayerCache {
        RenderTexture2D render_texture;
        bool is_dirty = true;
        bool is_visible = true;
        int blend_mode = BLEND_ALPHA;
        std::vector<RenderCommand> commands;
        uint64_t last_update_frame = 0;
    };

    LayerCache* getOrCreateLayerCache(uint8_t layer_id);
    void renderToLayerCache(LayerCache& cache);
    void compositeLayerCaches();

private:
    Config m_config;
    Stats m_stats;
    
    bool m_initialized = false;
    bool m_window_should_close = false;
    
    // Raylib resources
    Camera2D m_camera2d;
    bool m_using_camera2d = false;
    
    // Resource management
    std::unordered_map<uint32_t, Texture2D> m_textures;
    std::unordered_map<uint32_t, Font> m_fonts;
    std::unordered_map<uint8_t, LayerCache> m_layer_caches;
    
    // Batching system
    std::vector<BatchGroup> m_batch_groups;
    std::mutex m_batch_mutex;
    
    // Resource ID generation
    std::atomic<uint32_t> m_next_resource_id{1};
    
    // Default resources
    uint32_t m_default_font_id = 0;
    uint32_t m_white_texture_id = 0;
    
    // Performance tracking
    std::chrono::steady_clock::time_point m_frame_start_time;
    std::chrono::steady_clock::time_point m_last_fps_update;
    uint32_t m_frame_count_for_fps = 0;
    
    // Thread safety
    mutable std::mutex m_resource_mutex;
    mutable std::mutex m_stats_mutex;
    
    // Memory management
    size_t m_vertex_buffer_capacity = 0;
    size_t m_current_vertex_count = 0;
};

// Utility functions
Vector2 pointToVector2(const Point& point);
Point vector2ToPoint(const Vector2& vector);
::Color kairosColorToRaylib(const Kairos::Color& color);
Kairos::Color raylibColorToKairos(const ::Color& color);

} // namespace Kairos