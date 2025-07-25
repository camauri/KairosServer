// KairosServer/include/Graphics/BatchRenderer.hpp
#pragma once

#include <Types.hpp>
#include <raylib.h>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>

namespace Kairos {

/**
 * @brief High-performance batch renderer for optimized draw calls
 */
class BatchRenderer {
public:
    struct RenderBatch {
        uint32_t texture_id = 0;
        uint8_t layer_id = 0;
        int blend_mode = 0;
        Color tint = Color::WHITE;
        
        std::vector<TexturedVertex> vertices;
        std::vector<uint16_t> indices;
        
        size_t vertex_count = 0;
        size_t index_count = 0;
        
        void clear() {
            vertices.clear();
            indices.clear();
            vertex_count = 0;
            index_count = 0;
        }
        
        bool isEmpty() const {
            return vertex_count == 0;
        }
        
        bool isFull(size_t max_vertices) const {
            return vertex_count >= max_vertices;
        }
    };
    
    struct BatchKey {
        uint32_t texture_id;
        uint8_t layer_id;
        int blend_mode;
        uint32_t tint_rgba;
        
        bool operator==(const BatchKey& other) const {
            return texture_id == other.texture_id &&
                   layer_id == other.layer_id &&
                   blend_mode == other.blend_mode &&
                   tint_rgba == other.tint_rgba;
        }
    };
    
    struct BatchKeyHash {
        size_t operator()(const BatchKey& key) const {
            return ((size_t)key.texture_id << 32) |
                   ((size_t)key.layer_id << 24) |
                   ((size_t)key.blend_mode << 16) |
                   (size_t)key.tint_rgba;
        }
    };

public:
    explicit BatchRenderer(size_t max_vertices_per_batch = 10000);
    ~BatchRenderer();
    
    // Initialization
    bool initialize();
    void shutdown();
    
    // Batch management
    void beginFrame();
    void endFrame();
    void flushAll();
    void flushLayer(uint8_t layer_id);
    void flushTexture(uint32_t texture_id);
    
    // Quad rendering (most common operation)
    void drawQuad(const Rectangle& dest_rect, const Rectangle& source_rect,
                  uint32_t texture_id, const Color& tint = Color::WHITE, uint8_t layer_id = 0);
    
    void drawQuad(const Point& position, float width, float height,
                  uint32_t texture_id, const Color& tint = Color::WHITE, uint8_t layer_id = 0);
    
    void drawQuadRotated(const Point& position, float width, float height, float rotation,
                        uint32_t texture_id, const Color& tint = Color::WHITE, uint8_t layer_id = 0);
    
    // Advanced quad rendering
    void drawQuadTransformed(const std::vector<Point>& corners, const Rectangle& source_rect,
                            uint32_t texture_id, const Color& tint = Color::WHITE, uint8_t layer_id = 0);
    
    void drawQuadGradient(const Rectangle& dest_rect, const Rectangle& source_rect,
                         uint32_t texture_id, const std::vector<Color>& corner_colors, uint8_t layer_id = 0);
    
    // Multi-quad rendering (for sprites, UI elements, etc.)
    void drawQuads(const std::vector<TexturedVertex>& vertices, uint32_t texture_id,
                   const Color& tint = Color::WHITE, uint8_t layer_id = 0);
    
    void drawQuadBatch(const std::vector<Rectangle>& dest_rects, const std::vector<Rectangle>& source_rects,
                      uint32_t texture_id, const Color& tint = Color::WHITE, uint8_t layer_id = 0);
    
    // Generic vertex rendering
    void drawVertices(const std::vector<TexturedVertex>& vertices, const std::vector<uint16_t>& indices,
                     uint32_t texture_id, const Color& tint = Color::WHITE, uint8_t layer_id = 0);
    
    // Rendering state
    void setDefaultBlendMode(int blend_mode);
    void pushBlendMode(int blend_mode);
    void popBlendMode();
    void setClipRegion(const Rectangle& clip_rect);
    void clearClipRegion();
    
    // Optimization controls
    void setAutoFlushThreshold(size_t vertex_count);
    void setLayerSorting(bool enabled);
    void setTextureAtlasing(bool enabled);
    void setBatchMerging(bool enabled);
    
    // Statistics
    struct Stats {
        uint64_t total_vertices = 0;
        uint64_t total_indices = 0;
        uint64_t draw_calls_issued = 0;
        uint64_t batches_created = 0;
        uint64_t batches_merged = 0;
        uint64_t batches_flushed = 0;
        uint64_t vertices_flushed = 0;
        
        uint32_t active_batches = 0;
        uint32_t peak_batches = 0;
        size_t memory_usage_bytes = 0;
        
        double avg_vertices_per_batch = 0.0;
        double batch_efficiency = 0.0; // % of batches that were full
    };
    
    const Stats& getStats() const { return m_stats; }
    void resetStats();
    
    // Debug and profiling
    void enableDebugMode(bool enabled);
    void setDebugOverlayColor(const Color& color);
    std::string getBatchReport() const;

private:
    // Internal batch management
    RenderBatch* findOrCreateBatch(const BatchKey& key);
    void addVertexToBatch(RenderBatch* batch, const TexturedVertex& vertex);
    void addQuadToBatch(RenderBatch* batch, const std::vector<TexturedVertex>& quad_vertices);
    
    // Optimization
    void optimizeBatches();
    void mergeBatches();
    void sortBatchesByLayer();
    bool canMergeBatches(const RenderBatch& batch1, const RenderBatch& batch2);
    
    // Rendering
    void flushBatch(RenderBatch& batch);
    void renderBatch(const RenderBatch& batch);
    void setupRenderState(const RenderBatch& batch);
    
    // Vertex generation helpers
    void generateQuadVertices(const Rectangle& dest_rect, const Rectangle& source_rect,
                             const Color& tint, std::vector<TexturedVertex>& vertices);
    void generateQuadVerticesRotated(const Point& position, float width, float height, float rotation,
                                    const Rectangle& source_rect, const Color& tint,
                                    std::vector<TexturedVertex>& vertices);
    
    // Memory management
    void ensureBatchCapacity(RenderBatch* batch, size_t additional_vertices);
    void compactBatches();
    void releaseBatch(RenderBatch* batch);

private:
    // Configuration
    size_t m_max_vertices_per_batch;
    size_t m_auto_flush_threshold;
    bool m_layer_sorting_enabled = true;
    bool m_texture_atlasing_enabled = true;
    bool m_batch_merging_enabled = true;
    bool m_debug_mode_enabled = false;
    
    // Rendering state
    std::vector<int> m_blend_mode_stack;
    int m_current_blend_mode = 0; // BLEND_ALPHA
    Rectangle m_clip_region = {0, 0, 0, 0};
    bool m_clipping_enabled = false;
    
    // Batch storage
    std::unordered_map<BatchKey, std::unique_ptr<RenderBatch>, BatchKeyHash> m_batches;
    std::vector<RenderBatch*> m_render_order;
    
    // Memory pools
    std::vector<std::unique_ptr<RenderBatch>> m_batch_pool;
    size_t m_next_batch_pool_index = 0;
    
    // Thread safety
    mutable std::mutex m_batch_mutex;
    
    // Statistics
    Stats m_stats;
    
    // Debug
    Color m_debug_overlay_color = {255, 0, 0, 128};
};

/**
 * @brief Batch optimization utilities
 */
class BatchOptimizer {
public:
    struct OptimizationSettings {
        bool enable_texture_sorting = true;
        bool enable_layer_sorting = true;
        bool enable_state_sorting = true;
        bool enable_batch_merging = true;
        float merge_distance_threshold = 0.0f;
        size_t min_batch_size = 4;
        size_t max_batch_size = 10000;
    };
    
    static void optimizeBatchOrder(std::vector<BatchRenderer::RenderBatch*>& batches,
                                  const OptimizationSettings& settings);
    
    static bool shouldMergeBatches(const BatchRenderer::RenderBatch& batch1,
                                  const BatchRenderer::RenderBatch& batch2,
                                  const OptimizationSettings& settings);
    
    static void mergeBatches(BatchRenderer::RenderBatch& target,
                            const BatchRenderer::RenderBatch& source);
    
    static float calculateBatchScore(const BatchRenderer::RenderBatch& batch,
                                   const OptimizationSettings& settings);
};

/**
 * @brief Vertex utilities for batch rendering
 */
namespace BatchVertexUtils {
    // Quad vertex generation
    void generateQuadVertices(float x, float y, float width, float height,
                             float u1, float v1, float u2, float v2,
                             const Color& color, TexturedVertex* vertices);
    
    void generateQuadVerticesRotated(float x, float y, float width, float height, float rotation,
                                    float u1, float v1, float u2, float v2,
                                    const Color& color, TexturedVertex* vertices);
    
    void generateQuadVerticesTransformed(const Point corners[4],
                                        float u1, float v1, float u2, float v2,
                                        const Color& color, TexturedVertex* vertices);
    
    // Index generation
    void generateQuadIndices(uint16_t base_vertex_index, uint16_t* indices);
    void generateQuadIndices(uint16_t base_vertex_index, std::vector<uint16_t>& indices);
    
    // Color utilities
    void multiplyVertexColors(std::vector<TexturedVertex>& vertices, const Color& tint);
    void interpolateVertexColors(std::vector<TexturedVertex>& vertices,
                                const std::vector<Color>& corner_colors);
    
    // Transform utilities
    void transformVertices(std::vector<TexturedVertex>& vertices, const Matrix& transform);
    void rotateVertices(std::vector<TexturedVertex>& vertices, float angle, const Point& center);
    void scaleVertices(std::vector<TexturedVertex>& vertices, float scale_x, float scale_y, const Point& center);
}

} // namespace Kairos