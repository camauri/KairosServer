// KairosServer/src/Graphics/BatchRenderer.cpp
#include <Graphics/BatchRenderer.hpp>
#include <Utils/Logger.hpp>
#include <algorithm>
#include <cmath>

namespace Kairos {

BatchRenderer::BatchRenderer(size_t max_vertices_per_batch) 
    : m_max_vertices_per_batch(max_vertices_per_batch),
      m_auto_flush_threshold(max_vertices_per_batch / 2) {
    
    m_blend_mode_stack.push_back(0); // Default BLEND_ALPHA
    m_current_blend_mode = 0;
    
    Logger::debug("BatchRenderer created with max {} vertices per batch", max_vertices_per_batch);
}

BatchRenderer::~BatchRenderer() {
    if (!m_batches.empty()) {
        Logger::warning("BatchRenderer destroyed with {} pending batches", m_batches.size());
        flushAll();
    }
}

bool BatchRenderer::initialize() {
    Logger::info("Initializing BatchRenderer...");
    
    // Reserve space for batches
    m_render_order.reserve(256);
    
    // Create initial batch pool
    for (size_t i = 0; i < 16; ++i) {
        auto batch = std::make_unique<RenderBatch>();
        batch->vertices.reserve(m_max_vertices_per_batch);
        batch->indices.reserve(m_max_vertices_per_batch * 3 / 2); // Estimate for triangles
        m_batch_pool.push_back(std::move(batch));
    }
    
    Logger::info("BatchRenderer initialized successfully");
    return true;
}

void BatchRenderer::shutdown() {
    Logger::info("Shutting down BatchRenderer...");
    
    flushAll();
    m_batches.clear();
    m_render_order.clear();
    m_batch_pool.clear();
    
    Logger::info("BatchRenderer shutdown complete");
}

void BatchRenderer::beginFrame() {
    // Clear render order but keep batches for reuse
    m_render_order.clear();
    
    // Mark all batches as not needing flush
    for (auto& [key, batch] : m_batches) {
        batch->clear();
    }
    
    // Reset statistics for this frame
    m_stats.batches_created = 0;
    m_stats.batches_merged = 0;
    m_stats.active_batches = 0;
}

void BatchRenderer::endFrame() {
    // Optimize batches before rendering
    if (m_batch_merging_enabled) {
        optimizeBatches();
    }
    
    // Sort by layer if enabled
    if (m_layer_sorting_enabled) {
        sortBatchesByLayer();
    }
    
    // Flush all batches
    flushAll();
    
    // Update statistics
    m_stats.peak_batches = std::max(m_stats.peak_batches, m_stats.active_batches);
    if (m_stats.batches_flushed > 0) {
        m_stats.avg_vertices_per_batch = static_cast<double>(m_stats.vertices_flushed) / m_stats.batches_flushed;
    }
}

void BatchRenderer::flushAll() {
    std::lock_guard<std::mutex> lock(m_batch_mutex);
    
    for (auto* batch : m_render_order) {
        if (batch && !batch->isEmpty()) {
            flushBatch(*batch);
        }
    }
    
    // Clear render order for next frame
    m_render_order.clear();
    
    // Return batches to pool
    for (auto& [key, batch] : m_batches) {
        releaseBatch(batch.get());
    }
    m_batches.clear();
}

void BatchRenderer::flushLayer(uint8_t layer_id) {
    std::lock_guard<std::mutex> lock(m_batch_mutex);
    
    for (auto* batch : m_render_order) {
        if (batch && batch->layer_id == layer_id && !batch->isEmpty()) {
            flushBatch(*batch);
        }
    }
}

void BatchRenderer::flushTexture(uint32_t texture_id) {
    std::lock_guard<std::mutex> lock(m_batch_mutex);
    
    for (auto* batch : m_render_order) {
        if (batch && batch->texture_id == texture_id && !batch->isEmpty()) {
            flushBatch(*batch);
        }
    }
}

void BatchRenderer::drawQuad(const Rectangle& dest_rect, const Rectangle& source_rect,
                            uint32_t texture_id, const Color& tint, uint8_t layer_id) {
    std::vector<TexturedVertex> vertices(4);
    generateQuadVertices(dest_rect, source_rect, tint, vertices);
    
    drawQuads(vertices, texture_id, tint, layer_id);
}

void BatchRenderer::drawQuad(const Point& position, float width, float height,
                            uint32_t texture_id, const Color& tint, uint8_t layer_id) {
    Rectangle dest_rect = {position.x, position.y, width, height};
    Rectangle source_rect = {0, 0, 1, 1}; // Full texture
    
    drawQuad(dest_rect, source_rect, texture_id, tint, layer_id);
}

void BatchRenderer::drawQuadRotated(const Point& position, float width, float height, float rotation,
                                   uint32_t texture_id, const Color& tint, uint8_t layer_id) {
    std::vector<TexturedVertex> vertices(4);
    Rectangle source_rect = {0, 0, 1, 1}; // Full texture
    generateQuadVerticesRotated(position, width, height, rotation, source_rect, tint, vertices);
    
    drawQuads(vertices, texture_id, tint, layer_id);
}

void BatchRenderer::drawQuadTransformed(const std::vector<Point>& corners, const Rectangle& source_rect,
                                       uint32_t texture_id, const Color& tint, uint8_t layer_id) {
    if (corners.size() != 4) {
        Logger::warning("drawQuadTransformed requires exactly 4 corner points");
        return;
    }
    
    std::vector<TexturedVertex> vertices(4);
    
    vertices[0] = {corners[0].x, corners[0].y, source_rect.x, source_rect.y, tint.rgba};
    vertices[1] = {corners[1].x, corners[1].y, source_rect.x + source_rect.width, source_rect.y, tint.rgba};
    vertices[2] = {corners[2].x, corners[2].y, source_rect.x + source_rect.width, source_rect.y + source_rect.height, tint.rgba};
    vertices[3] = {corners[3].x, corners[3].y, source_rect.x, source_rect.y + source_rect.height, tint.rgba};
    
    drawQuads(vertices, texture_id, tint, layer_id);
}

void BatchRenderer::drawQuadGradient(const Rectangle& dest_rect, const Rectangle& source_rect,
                                    uint32_t texture_id, const std::vector<Color>& corner_colors, uint8_t layer_id) {
    if (corner_colors.size() != 4) {
        Logger::warning("drawQuadGradient requires exactly 4 corner colors");
        return;
    }
    
    std::vector<TexturedVertex> vertices(4);
    
    vertices[0] = {dest_rect.x, dest_rect.y, source_rect.x, source_rect.y, corner_colors[0].rgba};
    vertices[1] = {dest_rect.x + dest_rect.width, dest_rect.y, source_rect.x + source_rect.width, source_rect.y, corner_colors[1].rgba};
    vertices[2] = {dest_rect.x + dest_rect.width, dest_rect.y + dest_rect.height, source_rect.x + source_rect.width, source_rect.y + source_rect.height, corner_colors[2].rgba};
    vertices[3] = {dest_rect.x, dest_rect.y + dest_rect.height, source_rect.x, source_rect.y + source_rect.height, corner_colors[3].rgba};
    
    // Use white tint since colors are already in vertices
    drawQuads(vertices, texture_id, Color::WHITE, layer_id);
}

void BatchRenderer::drawQuads(const std::vector<TexturedVertex>& vertices, uint32_t texture_id,
                             const Color& tint, uint8_t layer_id) {
    if (vertices.empty() || vertices.size() % 4 != 0) {
        Logger::warning("drawQuads requires vertices in multiples of 4");
        return;
    }
    
    // Create batch key
    BatchKey key = {texture_id, layer_id, m_current_blend_mode, tint.rgba};
    
    // Find or create batch
    RenderBatch* batch = findOrCreateBatch(key);
    if (!batch) {
        Logger::error("Failed to create or find batch for quad rendering");
        return;
    }
    
    // Add vertices to batch
    for (size_t i = 0; i < vertices.size(); i += 4) {
        addQuadToBatch(batch, {vertices.begin() + i, vertices.begin() + i + 4});
        
        // Auto-flush if batch is getting full
        if (batch->vertex_count >= m_auto_flush_threshold) {
            flushBatch(*batch);
        }
    }
    
    m_stats.total_vertices += vertices.size();
}

void BatchRenderer::drawQuadBatch(const std::vector<Rectangle>& dest_rects, const std::vector<Rectangle>& source_rects,
                                 uint32_t texture_id, const Color& tint, uint8_t layer_id) {
    if (dest_rects.size() != source_rects.size()) {
        Logger::warning("drawQuadBatch: dest_rects and source_rects must have same size");
        return;
    }
    
    std::vector<TexturedVertex> all_vertices;
    all_vertices.reserve(dest_rects.size() * 4);
    
    for (size_t i = 0; i < dest_rects.size(); ++i) {
        std::vector<TexturedVertex> quad_vertices(4);
        generateQuadVertices(dest_rects[i], source_rects[i], tint, quad_vertices);
        all_vertices.insert(all_vertices.end(), quad_vertices.begin(), quad_vertices.end());
    }
    
    drawQuads(all_vertices, texture_id, tint, layer_id);
}

void BatchRenderer::drawVertices(const std::vector<TexturedVertex>& vertices, const std::vector<uint16_t>& indices,
                                uint32_t texture_id, const Color& tint, uint8_t layer_id) {
    if (vertices.empty() || indices.empty()) {
        return;
    }
    
    // Create batch key
    BatchKey key = {texture_id, layer_id, m_current_blend_mode, tint.rgba};
    
    // Find or create batch
    RenderBatch* batch = findOrCreateBatch(key);
    if (!batch) {
        Logger::error("Failed to create or find batch for vertex rendering");
        return;
    }
    
    // Add vertices and indices to batch
    size_t vertex_offset = batch->vertices.size();
    batch->vertices.insert(batch->vertices.end(), vertices.begin(), vertices.end());
    
    // Adjust indices for vertex offset
    for (uint16_t index : indices) {
        batch->indices.push_back(static_cast<uint16_t>(vertex_offset + index));
    }
    
    batch->vertex_count += vertices.size();
    batch->index_count += indices.size();
    
    // Apply tint to vertices if needed
    if (tint.rgba != Color::WHITE.rgba) {
        BatchVertexUtils::multiplyVertexColors(
            {batch->vertices.end() - vertices.size(), batch->vertices.end()}, tint);
    }
    
    m_stats.total_vertices += vertices.size();
    m_stats.total_indices += indices.size();
    
    // Auto-flush if batch is getting full
    if (batch->isFull(m_max_vertices_per_batch)) {
        flushBatch(*batch);
    }
}

void BatchRenderer::setDefaultBlendMode(int blend_mode) {
    m_blend_mode_stack[0] = blend_mode;
    if (m_blend_mode_stack.size() == 1) {
        m_current_blend_mode = blend_mode;
    }
}

void BatchRenderer::pushBlendMode(int blend_mode) {
    m_blend_mode_stack.push_back(blend_mode);
    m_current_blend_mode = blend_mode;
}

void BatchRenderer::popBlendMode() {
    if (m_blend_mode_stack.size() > 1) {
        m_blend_mode_stack.pop_back();
        m_current_blend_mode = m_blend_mode_stack.back();
    }
}

void BatchRenderer::setClipRegion(const Rectangle& clip_rect) {
    m_clip_region = clip_rect;
    m_clipping_enabled = true;
}

void BatchRenderer::clearClipRegion() {
    m_clipping_enabled = false;
}

void BatchRenderer::setAutoFlushThreshold(size_t vertex_count) {
    m_auto_flush_threshold = std::min(vertex_count, m_max_vertices_per_batch);
}

void BatchRenderer::setLayerSorting(bool enabled) {
    m_layer_sorting_enabled = enabled;
}

void BatchRenderer::setTextureAtlasing(bool enabled) {
    m_texture_atlasing_enabled = enabled;
}

void BatchRenderer::setBatchMerging(bool enabled) {
    m_batch_merging_enabled = enabled;
}

void BatchRenderer::resetStats() {
    m_stats = Stats{};
    Logger::debug("BatchRenderer statistics reset");
}

void BatchRenderer::enableDebugMode(bool enabled) {
    m_debug_mode_enabled = enabled;
    if (enabled) {
        Logger::info("BatchRenderer debug mode enabled");
    }
}

void BatchRenderer::setDebugOverlayColor(const Color& color) {
    m_debug_overlay_color = color;
}

std::string BatchRenderer::getBatchReport() const {
    std::lock_guard<std::mutex> lock(m_batch_mutex);
    
    std::stringstream ss;
    ss << "BatchRenderer Report:\n";
    ss << "Active batches: " << m_batches.size() << "\n";
    ss << "Peak batches: " << m_stats.peak_batches << "\n";
    ss << "Total vertices: " << m_stats.total_vertices << "\n";
    ss << "Total indices: " << m_stats.total_indices << "\n";
    ss << "Draw calls issued: " << m_stats.draw_calls_issued << "\n";
    ss << "Batches flushed: " << m_stats.batches_flushed << "\n";
    ss << "Avg vertices per batch: " << m_stats.avg_vertices_per_batch << "\n";
    ss << "Memory usage: " << (m_stats.memory_usage_bytes / 1024) << " KB\n";
    
    return ss.str();
}

// Private methods implementation

BatchRenderer::RenderBatch* BatchRenderer::findOrCreateBatch(const BatchKey& key) {
    auto it = m_batches.find(key);
    if (it != m_batches.end()) {
        return it->second.get();
    }
    
    // Try to get batch from pool
    std::unique_ptr<RenderBatch> batch;
    if (m_next_batch_pool_index < m_batch_pool.size()) {
        batch = std::move(m_batch_pool[m_next_batch_pool_index++]);
    } else {
        // Create new batch
        batch = std::make_unique<RenderBatch>();
        batch->vertices.reserve(m_max_vertices_per_batch);
        batch->indices.reserve(m_max_vertices_per_batch * 3 / 2);
    }
    
    // Configure batch
    batch->texture_id = key.texture_id;
    batch->layer_id = key.layer_id;
    batch->blend_mode = key.blend_mode;
    batch->tint = Color(key.tint_rgba);
    batch->clear();
    
    RenderBatch* batch_ptr = batch.get();
    m_batches[key] = std::move(batch);
    m_render_order.push_back(batch_ptr);
    
    m_stats.batches_created++;
    m_stats.active_batches++;
    
    return batch_ptr;
}

void BatchRenderer::addQuadToBatch(RenderBatch* batch, const std::vector<TexturedVertex>& quad_vertices) {
    if (quad_vertices.size() != 4) {
        return;
    }
    
    ensureBatchCapacity(batch, 4);
    
    // Add vertices
    size_t vertex_offset = batch->vertices.size();
    batch->vertices.insert(batch->vertices.end(), quad_vertices.begin(), quad_vertices.end());
    
    // Add indices for two triangles (quad)
    uint16_t indices[6] = {
        static_cast<uint16_t>(vertex_offset + 0),
        static_cast<uint16_t>(vertex_offset + 1),
        static_cast<uint16_t>(vertex_offset + 2),
        static_cast<uint16_t>(vertex_offset + 0),
        static_cast<uint16_t>(vertex_offset + 2),
        static_cast<uint16_t>(vertex_offset + 3)
    };
    
    batch->indices.insert(batch->indices.end(), indices, indices + 6);
    
    batch->vertex_count += 4;
    batch->index_count += 6;
}

void BatchRenderer::optimizeBatches() {
    if (!m_batch_merging_enabled || m_render_order.size() < 2) {
        return;
    }
    
    // Try to merge compatible batches
    for (size_t i = 0; i < m_render_order.size() - 1; ++i) {
        for (size_t j = i + 1; j < m_render_order.size(); ++j) {
            if (canMergeBatches(*m_render_order[i], *m_render_order[j])) {
                // Merge batch j into batch i
                auto& target = *m_render_order[i];
                auto& source = *m_render_order[j];
                
                size_t vertex_offset = target.vertices.size();
                target.vertices.insert(target.vertices.end(), source.vertices.begin(), source.vertices.end());
                
                // Adjust indices from source batch
                for (auto index : source.indices) {
                    target.indices.push_back(static_cast<uint16_t>(vertex_offset + index));
                }
                
                target.vertex_count += source.vertex_count;
    target.index_count += source.index_count;
}

float BatchOptimizer::calculateBatchScore(const BatchRenderer::RenderBatch& batch,
                                         const OptimizationSettings& settings) {
    float score = 0.0f;
    
    // Prefer larger batches (better GPU utilization)
    score += static_cast<float>(batch.vertex_count) / settings.max_batch_size * 100.0f;
    
    // Prefer batches with fewer state changes
    if (batch.blend_mode == 0) { // Default blend mode
        score += 10.0f;
    }
    
    // Prefer lower layers (rendered first)
    score += (255 - batch.layer_id) * 0.1f;
    
    return score;
}

// BatchVertexUtils implementation

namespace BatchVertexUtils {

void generateQuadVertices(float x, float y, float width, float height,
                         float u1, float v1, float u2, float v2,
                         const Color& color, TexturedVertex* vertices) {
    vertices[0] = {x, y, u1, v1, color.rgba};
    vertices[1] = {x + width, y, u2, v1, color.rgba};
    vertices[2] = {x + width, y + height, u2, v2, color.rgba};
    vertices[3] = {x, y + height, u1, v2, color.rgba};
}

void generateQuadVerticesRotated(float x, float y, float width, float height, float rotation,
                                float u1, float v1, float u2, float v2,
                                const Color& color, TexturedVertex* vertices) {
    float cos_r = std::cos(rotation);
    float sin_r = std::sin(rotation);
    float half_w = width * 0.5f;
    float half_h = height * 0.5f;
    float center_x = x + half_w;
    float center_y = y + half_h;
    
    // Calculate rotated corners
    Point corners[4] = {
        {-half_w, -half_h},
        {half_w, -half_h},
        {half_w, half_h},
        {-half_w, half_h}
    };
    
    for (int i = 0; i < 4; ++i) {
        float rotated_x = corners[i].x * cos_r - corners[i].y * sin_r;
        float rotated_y = corners[i].x * sin_r + corners[i].y * cos_r;
        
        corners[i].x = center_x + rotated_x;
        corners[i].y = center_y + rotated_y;
    }
    
    vertices[0] = {corners[0].x, corners[0].y, u1, v1, color.rgba};
    vertices[1] = {corners[1].x, corners[1].y, u2, v1, color.rgba};
    vertices[2] = {corners[2].x, corners[2].y, u2, v2, color.rgba};
    vertices[3] = {corners[3].x, corners[3].y, u1, v2, color.rgba};
}

void generateQuadVerticesTransformed(const Point corners[4],
                                    float u1, float v1, float u2, float v2,
                                    const Color& color, TexturedVertex* vertices) {
    vertices[0] = {corners[0].x, corners[0].y, u1, v1, color.rgba};
    vertices[1] = {corners[1].x, corners[1].y, u2, v1, color.rgba};
    vertices[2] = {corners[2].x, corners[2].y, u2, v2, color.rgba};
    vertices[3] = {corners[3].x, corners[3].y, u1, v2, color.rgba};
}

void generateQuadIndices(uint16_t base_vertex_index, uint16_t* indices) {
    indices[0] = base_vertex_index + 0;
    indices[1] = base_vertex_index + 1;
    indices[2] = base_vertex_index + 2;
    indices[3] = base_vertex_index + 0;
    indices[4] = base_vertex_index + 2;
    indices[5] = base_vertex_index + 3;
}

void generateQuadIndices(uint16_t base_vertex_index, std::vector<uint16_t>& indices) {
    indices.reserve(indices.size() + 6);
    indices.push_back(base_vertex_index + 0);
    indices.push_back(base_vertex_index + 1);
    indices.push_back(base_vertex_index + 2);
    indices.push_back(base_vertex_index + 0);
    indices.push_back(base_vertex_index + 2);
    indices.push_back(base_vertex_index + 3);
}

void multiplyVertexColors(std::vector<TexturedVertex>& vertices, const Color& tint) {
    for (auto& vertex : vertices) {
        Color vertex_color(vertex.color);
        vertex_color.r = static_cast<uint8_t>((vertex_color.r * tint.r) / 255);
        vertex_color.g = static_cast<uint8_t>((vertex_color.g * tint.g) / 255);
        vertex_color.b = static_cast<uint8_t>((vertex_color.b * tint.b) / 255);
        vertex_color.a = static_cast<uint8_t>((vertex_color.a * tint.a) / 255);
        vertex.color = vertex_color.rgba;
    }
}

void interpolateVertexColors(std::vector<TexturedVertex>& vertices,
                            const std::vector<Color>& corner_colors) {
    if (vertices.size() < 4 || corner_colors.size() < 4) {
        return;
    }
    
    // Simple interpolation for quad (assumes vertices are in order)
    for (size_t i = 0; i < vertices.size() && i < corner_colors.size(); ++i) {
        vertices[i].color = corner_colors[i].rgba;
    }
}

void transformVertices(std::vector<TexturedVertex>& vertices, const Matrix& transform) {
    // Note: This requires a Matrix type to be defined
    // For now, we'll implement a simple 2D transformation
    for (auto& vertex : vertices) {
        // Apply transformation matrix (simplified)
        float x = vertex.x;
        float y = vertex.y;
        
        // Assuming transform is a 2D matrix [a c tx; b d ty; 0 0 1]
        // vertex.x = transform.m0 * x + transform.m4 * y + transform.m12;
        // vertex.y = transform.m1 * x + transform.m5 * y + transform.m13;
        
        // Simplified for now - just identity
        vertex.x = x;
        vertex.y = y;
    }
}

void rotateVertices(std::vector<TexturedVertex>& vertices, float angle, const Point& center) {
    float cos_a = std::cos(angle);
    float sin_a = std::sin(angle);
    
    for (auto& vertex : vertices) {
        float dx = vertex.x - center.x;
        float dy = vertex.y - center.y;
        
        vertex.x = center.x + dx * cos_a - dy * sin_a;
        vertex.y = center.y + dx * sin_a + dy * cos_a;
    }
}

void scaleVertices(std::vector<TexturedVertex>& vertices, float scale_x, float scale_y, const Point& center) {
    for (auto& vertex : vertices) {
        vertex.x = center.x + (vertex.x - center.x) * scale_x;
        vertex.y = center.y + (vertex.y - center.y) * scale_y;
    }
}

} // namespace BatchVertexUtils

} // namespace Kairos;
                target.index_count += source.index_count;
                
                // Clear source batch
                source.clear();
                
                // Remove from render order
                m_render_order.erase(m_render_order.begin() + j);
                --j;
                
                m_stats.batches_merged++;
            }
        }
    }
}

void BatchRenderer::sortBatchesByLayer() {
    std::sort(m_render_order.begin(), m_render_order.end(),
              [](const RenderBatch* a, const RenderBatch* b) {
                  if (a->layer_id != b->layer_id) {
                      return a->layer_id < b->layer_id;
                  }
                  // Secondary sort by texture to minimize state changes
                  return a->texture_id < b->texture_id;
              });
}

bool BatchRenderer::canMergeBatches(const RenderBatch& batch1, const RenderBatch& batch2) {
    return batch1.texture_id == batch2.texture_id &&
           batch1.layer_id == batch2.layer_id &&
           batch1.blend_mode == batch2.blend_mode &&
           batch1.tint.rgba == batch2.tint.rgba &&
           batch1.vertex_count + batch2.vertex_count <= m_max_vertices_per_batch;
}

void BatchRenderer::flushBatch(RenderBatch& batch) {
    if (batch.isEmpty()) {
        return;
    }
    
    renderBatch(batch);
    
    m_stats.draw_calls_issued++;
    m_stats.batches_flushed++;
    m_stats.vertices_flushed += batch.vertex_count;
    
    batch.clear();
}

void BatchRenderer::renderBatch(const RenderBatch& batch) {
    setupRenderState(batch);
    
    // Use Raylib's immediate mode rendering for now
    // In a real implementation, this would use vertex buffers
    rlSetTexture(batch.texture_id);
    rlBegin(RL_TRIANGLES);
    
    for (size_t i = 0; i < batch.indices.size(); i += 3) {
        for (int j = 0; j < 3; ++j) {
            const auto& vertex = batch.vertices[batch.indices[i + j]];
            
            Color vertex_color = Color(vertex.color);
            rlColor4ub(vertex_color.r, vertex_color.g, vertex_color.b, vertex_color.a);
            rlTexCoord2f(vertex.u, vertex.v);
            rlVertex2f(vertex.x, vertex.y);
        }
    }
    
    rlEnd();
    rlSetTexture(0);
    
    // Debug overlay
    if (m_debug_mode_enabled) {
        // Draw batch boundaries
        rlBegin(RL_LINES);
        rlColor4ub(m_debug_overlay_color.r, m_debug_overlay_color.g, 
                   m_debug_overlay_color.b, m_debug_overlay_color.a);
        
        // Draw simple bounding box
        if (!batch.vertices.empty()) {
            float min_x = batch.vertices[0].x, max_x = batch.vertices[0].x;
            float min_y = batch.vertices[0].y, max_y = batch.vertices[0].y;
            
            for (const auto& vertex : batch.vertices) {
                min_x = std::min(min_x, vertex.x);
                max_x = std::max(max_x, vertex.x);
                min_y = std::min(min_y, vertex.y);
                max_y = std::max(max_y, vertex.y);
            }
            
            rlVertex2f(min_x, min_y);
            rlVertex2f(max_x, min_y);
            rlVertex2f(max_x, min_y);
            rlVertex2f(max_x, max_y);
            rlVertex2f(max_x, max_y);
            rlVertex2f(min_x, max_y);
            rlVertex2f(min_x, max_y);
            rlVertex2f(min_x, min_y);
        }
        
        rlEnd();
    }
}

void BatchRenderer::setupRenderState(const RenderBatch& batch) {
    // Set blend mode
    BeginBlendMode(batch.blend_mode);
    
    // Set clipping if enabled
    if (m_clipping_enabled) {
        BeginScissorMode(static_cast<int>(m_clip_region.x), static_cast<int>(m_clip_region.y),
                        static_cast<int>(m_clip_region.width), static_cast<int>(m_clip_region.height));
    }
}

void BatchRenderer::generateQuadVertices(const Rectangle& dest_rect, const Rectangle& source_rect,
                                        const Color& tint, std::vector<TexturedVertex>& vertices) {
    vertices.resize(4);
    
    vertices[0] = {dest_rect.x, dest_rect.y, source_rect.x, source_rect.y, tint.rgba};
    vertices[1] = {dest_rect.x + dest_rect.width, dest_rect.y, source_rect.x + source_rect.width, source_rect.y, tint.rgba};
    vertices[2] = {dest_rect.x + dest_rect.width, dest_rect.y + dest_rect.height, source_rect.x + source_rect.width, source_rect.y + source_rect.height, tint.rgba};
    vertices[3] = {dest_rect.x, dest_rect.y + dest_rect.height, source_rect.x, source_rect.y + source_rect.height, tint.rgba};
}

void BatchRenderer::generateQuadVerticesRotated(const Point& position, float width, float height, float rotation,
                                               const Rectangle& source_rect, const Color& tint,
                                               std::vector<TexturedVertex>& vertices) {
    vertices.resize(4);
    
    float cos_r = std::cos(rotation);
    float sin_r = std::sin(rotation);
    float half_w = width * 0.5f;
    float half_h = height * 0.5f;
    
    // Calculate rotated corners relative to center
    Point corners[4] = {
        {-half_w * cos_r - (-half_h) * sin_r, -half_w * sin_r + (-half_h) * cos_r},
        {half_w * cos_r - (-half_h) * sin_r, half_w * sin_r + (-half_h) * cos_r},
        {half_w * cos_r - half_h * sin_r, half_w * sin_r + half_h * cos_r},
        {-half_w * cos_r - half_h * sin_r, -half_w * sin_r + half_h * cos_r}
    };
    
    // Translate to final position
    for (int i = 0; i < 4; ++i) {
        corners[i].x += position.x + half_w;
        corners[i].y += position.y + half_h;
    }
    
    vertices[0] = {corners[0].x, corners[0].y, source_rect.x, source_rect.y, tint.rgba};
    vertices[1] = {corners[1].x, corners[1].y, source_rect.x + source_rect.width, source_rect.y, tint.rgba};
    vertices[2] = {corners[2].x, corners[2].y, source_rect.x + source_rect.width, source_rect.y + source_rect.height, tint.rgba};
    vertices[3] = {corners[3].x, corners[3].y, source_rect.x, source_rect.y + source_rect.height, tint.rgba};
}

void BatchRenderer::ensureBatchCapacity(RenderBatch* batch, size_t additional_vertices) {
    if (batch->vertices.capacity() < batch->vertices.size() + additional_vertices) {
        size_t new_capacity = std::max(batch->vertices.capacity() * 2, 
                                      batch->vertices.size() + additional_vertices);
        new_capacity = std::min(new_capacity, m_max_vertices_per_batch);
        
        batch->vertices.reserve(new_capacity);
        batch->indices.reserve(new_capacity * 3 / 2); // Estimate for triangles
    }
}

void BatchRenderer::releaseBatch(RenderBatch* batch) {
    if (batch && m_next_batch_pool_index > 0) {
        batch->clear();
        // Return to pool by moving to available slot
        // Simplified pool management
    }
}

// BatchOptimizer implementation

void BatchOptimizer::optimizeBatchOrder(std::vector<BatchRenderer::RenderBatch*>& batches,
                                       const OptimizationSettings& settings) {
    if (batches.size() < 2) {
        return;
    }
    
    // Sort by multiple criteria
    std::sort(batches.begin(), batches.end(), [&settings](const BatchRenderer::RenderBatch* a, const BatchRenderer::RenderBatch* b) {
        // Primary: layer ordering
        if (settings.enable_layer_sorting && a->layer_id != b->layer_id) {
            return a->layer_id < b->layer_id;
        }
        
        // Secondary: texture grouping
        if (settings.enable_texture_sorting && a->texture_id != b->texture_id) {
            return a->texture_id < b->texture_id;
        }
        
        // Tertiary: state grouping
        if (settings.enable_state_sorting && a->blend_mode != b->blend_mode) {
            return a->blend_mode < b->blend_mode;
        }
        
        return false; // Equal priority
    });
}

bool BatchOptimizer::shouldMergeBatches(const BatchRenderer::RenderBatch& batch1,
                                       const BatchRenderer::RenderBatch& batch2,
                                       const OptimizationSettings& settings) {
    // Check basic compatibility
    if (batch1.texture_id != batch2.texture_id ||
        batch1.layer_id != batch2.layer_id ||
        batch1.blend_mode != batch2.blend_mode) {
        return false;
    }
    
    // Check size limits
    if (batch1.vertex_count + batch2.vertex_count > settings.max_batch_size) {
        return false;
    }
    
    // Check minimum batch size
    if (batch1.vertex_count < settings.min_batch_size && 
        batch2.vertex_count < settings.min_batch_size) {
        return true;
    }
    
    return settings.enable_batch_merging;
}

void BatchOptimizer::mergeBatches(BatchRenderer::RenderBatch& target,
                                 const BatchRenderer::RenderBatch& source) {
    size_t vertex_offset = target.vertices.size();
    
    // Merge vertices
    target.vertices.insert(target.vertices.end(), source.vertices.begin(), source.vertices.end());
    
    // Merge indices with offset
    for (auto index : source.indices) {
        target.indices.push_back(static_cast<uint16_t>(vertex_offset + index));
    }
    
    target.vertex_count += source.vertex_count