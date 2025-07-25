// KairosServer/src/Graphics/PrimitiveRenderer.cpp
#include <Graphics/PrimitiveRenderer.hpp>
#include <Utils/Logger.hpp>
#include <cmath>
#include <algorithm>

namespace Kairos {

PrimitiveRenderer::PrimitiveRenderer() {
    Logger::debug("PrimitiveRenderer created");
}

PrimitiveRenderer::~PrimitiveRenderer() {
    if (!m_primitive_batches.empty()) {
        Logger::warning("PrimitiveRenderer destroyed with {} pending batches", m_primitive_batches.size());
    }
}

bool PrimitiveRenderer::initialize() {
    Logger::info("Initializing PrimitiveRenderer...");
    
    // Reserve space for batches and buffers
    m_primitive_batches.reserve(64);
    m_vertex_buffer.reserve(m_max_vertices_per_batch);
    m_index_buffer.reserve(m_max_indices_per_batch);
    
    Logger::info("PrimitiveRenderer initialized successfully");
    return true;
}

void PrimitiveRenderer::shutdown() {
    Logger::info("Shutting down PrimitiveRenderer...");
    
    flushBatches();
    m_primitive_batches.clear();
    m_vertex_buffer.clear();
    m_index_buffer.clear();
    
    Logger::info("PrimitiveRenderer shutdown complete");
}

void PrimitiveRenderer::drawPoint(const Point& position, const Color& color, float size) {
    std::vector<Point> points = {position};
    drawPoints(points, color, size);
}

void PrimitiveRenderer::drawPoints(const std::vector<Point>& points, const Color& color, float size) {
    std::vector<Color> colors(points.size(), color);
    drawPoints(points, colors, size);
}

void PrimitiveRenderer::drawPoints(const std::vector<Point>& points, const std::vector<Color>& colors, float size) {
    if (points.empty()) {
        return;
    }
    
    size_t color_count = std::min(points.size(), colors.size());
    
    // Generate vertices for points (as small quads)
    std::vector<TriangleVertex> vertices;
    std::vector<uint16_t> indices;
    
    vertices.reserve(points.size() * 4); // 4 vertices per point
    indices.reserve(points.size() * 6);  // 6 indices per point (2 triangles)
    
    float half_size = size * 0.5f;
    
    for (size_t i = 0; i < points.size(); ++i) {
        const Point& pos = points[i];
        const Color& point_color = (i < color_count) ? colors[i] : colors.back();
        
        uint16_t base_index = static_cast<uint16_t>(vertices.size());
        
        // Create quad vertices for point
        vertices.push_back({{pos.x - half_size, pos.y - half_size}, point_color});
        vertices.push_back({{pos.x + half_size, pos.y - half_size}, point_color});
        vertices.push_back({{pos.x + half_size, pos.y + half_size}, point_color});
        vertices.push_back({{pos.x - half_size, pos.y + half_size}, point_color});
        
        // Add indices for two triangles
        indices.insert(indices.end(), {
            base_index, static_cast<uint16_t>(base_index + 1), static_cast<uint16_t>(base_index + 2),
            base_index, static_cast<uint16_t>(base_index + 2), static_cast<uint16_t>(base_index + 3)
        });
    }
    
    addToBatch(PrimitiveBatch::QUADS, vertices, indices, m_current_layer);
    m_stats.points_rendered += points.size();
}

void PrimitiveRenderer::drawLine(const Point& start, const Point& end, const Color& color, float thickness) {
    std::vector<TriangleVertex> vertices;
    generateLineVertices(start, end, thickness, color, vertices);
    
    std::vector<uint16_t> indices;
    indices.reserve(6); // 2 triangles
    for (uint16_t i = 0; i < 6; ++i) {
        indices.push_back(i);
    }
    
    addToBatch(PrimitiveBatch::TRIANGLES, vertices, indices, m_current_layer);
    m_stats.lines_rendered++;
}

void PrimitiveRenderer::drawLines(const std::vector<Point>& points, const Color& color, float thickness) {
    if (points.size() < 2) {
        return;
    }
    
    for (size_t i = 0; i < points.size() - 1; ++i) {
        drawLine(points[i], points[i + 1], color, thickness);
    }
}

void PrimitiveRenderer::drawLineStrip(const std::vector<Point>& points, const Color& color, float thickness) {
    drawLines(points, color, thickness); // Same implementation for now
}

void PrimitiveRenderer::drawLineLoop(const std::vector<Point>& points, const Color& color, float thickness) {
    if (points.size() < 3) {
        return;
    }
    
    drawLines(points, color, thickness);
    // Close the loop
    drawLine(points.back(), points.front(), color, thickness);
}

void PrimitiveRenderer::drawRectangle(const Point& position, float width, float height, const Color& color, bool filled) {
    Rectangle rect = {position.x, position.y, width, height};
    drawRectangle(rect, color, filled);
}

void PrimitiveRenderer::drawRectangle(const Rectangle& rect, const Color& color, bool filled) {
    std::vector<TriangleVertex> vertices;
    generateRectangleVertices(rect, color, vertices);
    
    std::vector<uint16_t> indices;
    
    if (filled) {
        // Two triangles for filled rectangle
        indices = {0, 1, 2, 0, 2, 3};
    } else {
        // Four lines for rectangle outline
        drawLine({rect.x, rect.y}, {rect.x + rect.width, rect.y}, color, 1.0f);
        drawLine({rect.x + rect.width, rect.y}, {rect.x + rect.width, rect.y + rect.height}, color, 1.0f);
        drawLine({rect.x + rect.width, rect.y + rect.height}, {rect.x, rect.y + rect.height}, color, 1.0f);
        drawLine({rect.x, rect.y + rect.height}, {rect.x, rect.y}, color, 1.0f);
        return;
    }
    
    addToBatch(PrimitiveBatch::TRIANGLES, vertices, indices, m_current_layer);
}

void PrimitiveRenderer::drawRectangleGradient(const Rectangle& rect, const Color& top_left, const Color& top_right,
                                             const Color& bottom_left, const Color& bottom_right) {
    std::vector<TriangleVertex> vertices = {
        {{rect.x, rect.y}, top_left},
        {{rect.x + rect.width, rect.y}, top_right},
        {{rect.x + rect.width, rect.y + rect.height}, bottom_right},
        {{rect.x, rect.y + rect.height}, bottom_left}
    };
    
    std::vector<uint16_t> indices = {0, 1, 2, 0, 2, 3};
    
    addToBatch(PrimitiveBatch::TRIANGLES, vertices, indices, m_current_layer);
}

void PrimitiveRenderer::drawRectangleRounded(const Rectangle& rect, float radius, const Color& color, bool filled) {
    // Simplified rounded rectangle - use arcs at corners
    radius = std::min(radius, std::min(rect.width, rect.height) * 0.5f);
    
    if (filled) {
        // Central rectangle
        Rectangle center_rect = {rect.x + radius, rect.y, rect.width - 2 * radius, rect.height};
        drawRectangle(center_rect, color, true);
        
        // Left and right rectangles
        Rectangle left_rect = {rect.x, rect.y + radius, radius, rect.height - 2 * radius};
        Rectangle right_rect = {rect.x + rect.width - radius, rect.y + radius, radius, rect.height - 2 * radius};
        drawRectangle(left_rect, color, true);
        drawRectangle(right_rect, color, true);
        
        // Corner arcs
        drawArc({rect.x + radius, rect.y + radius}, radius, 180, 270, color, true);
        drawArc({rect.x + rect.width - radius, rect.y + radius}, radius, 270, 360, color, true);
        drawArc({rect.x + rect.width - radius, rect.y + rect.height - radius}, radius, 0, 90, color, true);
        drawArc({rect.x + radius, rect.y + rect.height - radius}, radius, 90, 180, color, true);
    } else {
        // Draw outline with rounded corners
        // Top and bottom lines
        drawLine({rect.x + radius, rect.y}, {rect.x + rect.width - radius, rect.y}, color);
        drawLine({rect.x + radius, rect.y + rect.height}, {rect.x + rect.width - radius, rect.y + rect.height}, color);
        
        // Left and right lines
        drawLine({rect.x, rect.y + radius}, {rect.x, rect.y + rect.height - radius}, color);
        drawLine({rect.x + rect.width, rect.y + radius}, {rect.x + rect.width, rect.y + rect.height - radius}, color);
        
        // Corner arcs
        drawArc({rect.x + radius, rect.y + radius}, radius, 180, 270, color, false);
        drawArc({rect.x + rect.width - radius, rect.y + radius}, radius, 270, 360, color, false);
        drawArc({rect.x + rect.width - radius, rect.y + rect.height - radius}, radius, 0, 90, color, false);
        drawArc({rect.x + radius, rect.y + rect.height - radius}, radius, 90, 180, color, false);
    }
}

void PrimitiveRenderer::drawCircle(const Point& center, float radius, const Color& color, bool filled) {
    std::vector<TriangleVertex> vertices;
    generateCircleVertices(center, radius, color, filled, 32, vertices);
    
    std::vector<uint16_t> indices;
    if (filled) {
        // Triangle fan from center
        indices.reserve((vertices.size() - 1) * 3);
        for (size_t i = 1; i < vertices.size() - 1; ++i) {
            indices.insert(indices.end(), {0, static_cast<uint16_t>(i), static_cast<uint16_t>(i + 1)});
        }
        // Close the fan
        indices.insert(indices.end(), {0, static_cast<uint16_t>(vertices.size() - 1), 1});
    } else {
        // Line loop around circumference
        indices.reserve(vertices.size() * 2);
        for (size_t i = 0; i < vertices.size(); ++i) {
            indices.push_back(static_cast<uint16_t>(i));
            indices.push_back(static_cast<uint16_t>((i + 1) % vertices.size()));
        }
    }
    
    addToBatch(filled ? PrimitiveBatch::TRIANGLES : PrimitiveBatch::LINES, vertices, indices, m_current_layer);
}

void PrimitiveRenderer::drawCircleGradient(const Point& center, float radius, const Color& inner, const Color& outer) {
    std::vector<TriangleVertex> vertices;
    
    // Center vertex with inner color
    vertices.push_back({center, inner});
    
    // Circumference vertices with outer color
    int segments = 32;
    for (int i = 0; i <= segments; ++i) {
        float angle = (2.0f * M_PI * i) / segments;
        Point pos = {
            center.x + radius * std::cos(angle),
            center.y + radius * std::sin(angle)
        };
        vertices.push_back({pos, outer});
    }
    
    // Triangle fan indices
    std::vector<uint16_t> indices;
    indices.reserve(segments * 3);
    for (int i = 1; i <= segments; ++i) {
        indices.insert(indices.end(), {0, static_cast<uint16_t>(i), static_cast<uint16_t>(i + 1)});
    }
    
    addToBatch(PrimitiveBatch::TRIANGLES, vertices, indices, m_current_layer);
}

void PrimitiveRenderer::drawEllipse(const Point& center, float radius_x, float radius_y, const Color& color, bool filled) {
    std::vector<TriangleVertex> vertices;
    
    int segments = 32;
    
    if (filled) {
        // Center vertex
        vertices.push_back({center, color});
    }
    
    // Generate ellipse points
    for (int i = 0; i <= segments; ++i) {
        float angle = (2.0f * M_PI * i) / segments;
        Point pos = {
            center.x + radius_x * std::cos(angle),
            center.y + radius_y * std::sin(angle)
        };
        vertices.push_back({pos, color});
    }
    
    std::vector<uint16_t> indices;
    if (filled) {
        // Triangle fan from center
        indices.reserve(segments * 3);
        for (int i = 1; i <= segments; ++i) {
            indices.insert(indices.end(), {0, static_cast<uint16_t>(i), static_cast<uint16_t>(i + 1)});
        }
    } else {
        // Line loop
        indices.reserve(segments * 2);
        for (int i = 0; i < segments; ++i) {
            indices.push_back(static_cast<uint16_t>(i));
            indices.push_back(static_cast<uint16_t>((i + 1) % segments));
        }
    }
    
    addToBatch(filled ? PrimitiveBatch::TRIANGLES : PrimitiveBatch::LINES, vertices, indices, m_current_layer);
}

void PrimitiveRenderer::drawArc(const Point& center, float radius, float start_angle, float end_angle, 
                               const Color& color, bool filled) {
    std::vector<TriangleVertex> vertices;
    
    // Convert angles to radians
    float start_rad = start_angle * M_PI / 180.0f;
    float end_rad = end_angle * M_PI / 180.0f;
    
    // Ensure positive angle range
    while (end_rad < start_rad) {
        end_rad += 2.0f * M_PI;
    }
    
    float angle_range = end_rad - start_rad;
    int segments = std::max(4, static_cast<int>(angle_range * 16.0f / (2.0f * M_PI)));
    
    if (filled) {
        // Center vertex for filled arc
        vertices.push_back({center, color});
    }
    
    // Generate arc points
    for (int i = 0; i <= segments; ++i) {
        float angle = start_rad + (angle_range * i) / segments;
        Point pos = {
            center.x + radius * std::cos(angle),
            center.y + radius * std::sin(angle)
        };
        vertices.push_back({pos, color});
    }
    
    std::vector<uint16_t> indices;
    if (filled) {
        // Triangle fan from center
        indices.reserve(segments * 3);
        for (int i = 1; i <= segments; ++i) {
            indices.insert(indices.end(), {0, static_cast<uint16_t>(i), static_cast<uint16_t>(i + 1)});
        }
    } else {
        // Line strip along arc
        indices.reserve(segments * 2);
        for (int i = 0; i < segments; ++i) {
            indices.push_back(static_cast<uint16_t>(i));
            indices.push_back(static_cast<uint16_t>(i + 1));
        }
    }
    
    addToBatch(filled ? PrimitiveBatch::TRIANGLES : PrimitiveBatch::LINES, vertices, indices, m_current_layer);
}

void PrimitiveRenderer::drawTriangle(const Point& p1, const Point& p2, const Point& p3, const Color& color, bool filled) {
    std::vector<Point> points = {p1, p2, p3};
    drawPolygon(points, color, filled);
}

void PrimitiveRenderer::drawPolygon(const std::vector<Point>& points, const Color& color, bool filled) {
    if (points.size() < 3) {
        return;
    }
    
    std::vector<TriangleVertex> vertices;
    generatePolygonVertices(points, color, filled, vertices);
    
    std::vector<uint16_t> indices;
    if (filled) {
        // Triangulate the polygon
        indices = triangulatePolygon(points);
    } else {
        // Line loop around perimeter
        indices.reserve(points.size() * 2);
        for (size_t i = 0; i < points.size(); ++i) {
            indices.push_back(static_cast<uint16_t>(i));
            indices.push_back(static_cast<uint16_t>((i + 1) % points.size()));
        }
    }
    
    addToBatch(filled ? PrimitiveBatch::TRIANGLES : PrimitiveBatch::LINES, vertices, indices, m_current_layer);
}

void PrimitiveRenderer::drawPolygonGradient(const std::vector<Point>& points, const std::vector<Color>& colors, bool filled) {
    if (points.size() < 3 || colors.empty()) {
        return;
    }
    
    std::vector<TriangleVertex> vertices;
    vertices.reserve(points.size());
    
    for (size_t i = 0; i < points.size(); ++i) {
        const Color& vertex_color = (i < colors.size()) ? colors[i] : colors.back();
        vertices.push_back({points[i], vertex_color});
    }
    
    std::vector<uint16_t> indices;
    if (filled) {
        indices = triangulatePolygon(points);
    } else {
        // Line loop
        indices.reserve(points.size() * 2);
        for (size_t i = 0; i < points.size(); ++i) {
            indices.push_back(static_cast<uint16_t>(i));
            indices.push_back(static_cast<uint16_t>((i + 1) % points.size()));
        }
    }
    
    addToBatch(filled ? PrimitiveBatch::TRIANGLES : PrimitiveBatch::LINES, vertices, indices, m_current_layer);
}

void PrimitiveRenderer::drawBezierQuadratic(const Point& start, const Point& control, const Point& end, 
                                           const Color& color, float thickness, int segments) {
    std::vector<Point> curve_points = PrimitiveGeometry::generateBezierPoints(start, control, end, segments);
    drawLineStrip(curve_points, color, thickness);
}

void PrimitiveRenderer::drawBezierCubic(const Point& start, const Point& control1, const Point& control2, const Point& end,
                                       const Color& color, float thickness, int segments) {
    std::vector<Point> curve_points = PrimitiveGeometry::generateBezierPoints(start, control1, control2, end, segments);
    drawLineStrip(curve_points, color, thickness);
}

void PrimitiveRenderer::drawSpline(const std::vector<Point>& points, const Color& color, float thickness) {
    if (points.size() < 2) {
        return;
    }
    
    std::vector<Point> spline_points = PrimitiveGeometry::generateSplinePoints(points, 10);
    drawLineStrip(spline_points, color, thickness);
}

void PrimitiveRenderer::beginBatch(uint8_t layer_id) {
    m_in_batch = true;
    m_current_layer = layer_id;
}

void PrimitiveRenderer::endBatch() {
    flushBatches();
    m_in_batch = false;
}

void PrimitiveRenderer::flushBatches() {
    for (auto& batch : m_primitive_batches) {
        if (!batch.isEmpty()) {
            flushBatch(batch);
        }
    }
    m_primitive_batches.clear();
}

void PrimitiveRenderer::flushLayer(uint8_t layer_id) {
    for (auto& batch : m_primitive_batches) {
        if (batch.layer_id == layer_id && !batch.isEmpty()) {
            flushBatch(batch);
        }
    }
    
    // Remove flushed batches
    m_primitive_batches.erase(
        std::remove_if(m_primitive_batches.begin(), m_primitive_batches.end(),
                      [layer_id](const PrimitiveBatch& batch) {
                          return batch.layer_id == layer_id && batch.isEmpty();
                      }),
        m_primitive_batches.end());
}

void PrimitiveRenderer::setAntialiasing(bool enabled) {
    m_antialiasing_enabled = enabled;
}

void PrimitiveRenderer::setLineJoinStyle(int join_style) {
    m_line_join_style = join_style;
}

void PrimitiveRenderer::setLineCapStyle(int cap_style) {
    m_line_cap_style = cap_style;
}

void PrimitiveRenderer::setBlendMode(int blend_mode) {
    m_blend_mode = blend_mode;
}

void PrimitiveRenderer::resetStats() {
    m_stats = Stats{};
    Logger::debug("PrimitiveRenderer statistics reset");
}

// Private methods implementation

void PrimitiveRenderer::generateLineVertices(const Point& start, const Point& end, float thickness, 
                                            const Color& color, std::vector<TriangleVertex>& vertices) {
    vertices.clear();
    vertices.reserve(4); // Rectangle for thick line
    
    // Calculate perpendicular vector
    Point direction = {end.x - start.x, end.y - start.y};
    float length = std::sqrt(direction.x * direction.x + direction.y * direction.y);
    
    if (length < 0.001f) {
        // Degenerate line, create a small point
        float half_thickness = thickness * 0.5f;
        vertices.push_back({{start.x - half_thickness, start.y - half_thickness}, color});
        vertices.push_back({{start.x + half_thickness, start.y - half_thickness}, color});
        vertices.push_back({{start.x + half_thickness, start.y + half_thickness}, color});
        vertices.push_back({{start.x - half_thickness, start.y + half_thickness}, color});
        return;
    }
    
    // Normalize direction
    direction.x /= length;
    direction.y /= length;
    
    // Calculate perpendicular (normal)
    Point normal = {-direction.y, direction.x};
    normal.x *= thickness * 0.5f;
    normal.y *= thickness * 0.5f;
    
    // Create quad vertices
    vertices.push_back({{start.x - normal.x, start.y - normal.y}, color});
    vertices.push_back({{start.x + normal.x, start.y + normal.y}, color});
    vertices.push_back({{end.x + normal.x, end.y + normal.y}, color});
    vertices.push_back({{end.x - normal.x, end.y - normal.y}, color});
}

void PrimitiveRenderer::generateCircleVertices(const Point& center, float radius, const Color& color,
                                              bool filled, int segments, std::vector<TriangleVertex>& vertices) {
    vertices.clear();
    
    if (filled) {
        vertices.reserve(segments + 2);
        // Center vertex
        vertices.push_back({center, color});
    } else {
        vertices.reserve(segments + 1);
    }
    
    // Generate circumference vertices
    for (int i = 0; i <= segments; ++i) {
        float angle = (2.0f * M_PI * i) / segments;
        Point pos = {
            center.x + radius * std::cos(angle),
            center.y + radius * std::sin(angle)
        };
        vertices.push_back({pos, color});
    }
}

void PrimitiveRenderer::generateRectangleVertices(const Rectangle& rect, const Color& color,
                                                 std::vector<TriangleVertex>& vertices) {
    vertices.clear();
    vertices.reserve(4);
    
    vertices.push_back({{rect.x, rect.y}, color});
    vertices.push_back({{rect.x + rect.width, rect.y}, color});
    vertices.push_back({{rect.x + rect.width, rect.y + rect.height}, color});
    vertices.push_back({{rect.x, rect.y + rect.height}, color});
}

void PrimitiveRenderer::generatePolygonVertices(const std::vector<Point>& points, const Color& color,
                                               bool filled, std::vector<TriangleVertex>& vertices) {
    vertices.clear();
    vertices.reserve(points.size());
    
    for (const Point& point : points) {
        vertices.push_back({point, color});
    }
}

std::vector<uint16_t> PrimitiveRenderer::triangulatePolygon(const std::vector<Point>& points) {
    // Simple ear clipping triangulation
    std::vector<uint16_t> indices;
    
    if (points.size() < 3) {
        return indices;
    }
    
    if (points.size() == 3) {
        // Already a triangle
        indices = {0, 1, 2};
        return indices;
    }
    
    if (points.size() == 4) {
        // Quad - split into two triangles
        indices = {0, 1, 2, 0, 2, 3};
        return indices;
    }
    
    // For complex polygons, use a simple fan triangulation from first vertex
    // This works for convex polygons
    indices.reserve((points.size() - 2) * 3);
    for (size_t i = 1; i < points.size() - 1; ++i) {
        indices.insert(indices.end(), {0, static_cast<uint16_t>(i), static_cast<uint16_t>(i + 1)});
    }
    
    return indices;
}

void PrimitiveRenderer::tessellateComplexPolygon(const std::vector<Point>& points, 
                                                std::vector<TriangleVertex>& vertices,
                                                std::vector<uint16_t>& indices) {
    // Placeholder for complex polygon tessellation
    // In a full implementation, this would use a proper tessellation library
    generatePolygonVertices(points, Color::WHITE, true, vertices);
    indices = triangulatePolygon(points);
}

void PrimitiveRenderer::addToBatch(PrimitiveBatch::Type type, const std::vector<TriangleVertex>& vertices,
                                  const std::vector<uint16_t>& indices, uint8_t layer_id) {
    // Find existing batch of same type and layer
    PrimitiveBatch* target_batch = nullptr;
    
    for (auto& batch : m_primitive_batches) {
        if (batch.type == type && batch.layer_id == layer_id &&
            batch.vertices.size() + vertices.size() <= m_max_vertices_per_batch &&
            batch.indices.size() + indices.size() <= m_max_indices_per_batch) {
            target_batch = &batch;
            break;
        }
    }
    
    if (!target_batch) {
        // Create new batch
        m_primitive_batches.emplace_back();
        target_batch = &m_primitive_batches.back();
        target_batch->type = type;
        target_batch->layer_id = layer_id;
    }
    
    // Add vertices and indices to batch
    size_t vertex_offset = target_batch->vertices.size();
    target_batch->vertices.insert(target_batch->vertices.end(), vertices.begin(), vertices.end());
    
    // Adjust indices for vertex offset
    for (uint16_t index : indices) {
        target_batch->indices.push_back(static_cast<uint16_t>(vertex_offset + index));
    }
    
    m_stats.vertices_processed += vertices.size();
}

void PrimitiveRenderer::flushBatch(PrimitiveBatch& batch) {
    if (batch.isEmpty()) {
        return;
    }
    
    optimizeBatch(batch);
    
    // Set blend mode
    BeginBlendMode(m_blend_mode);
    
    // Render based on batch type
    switch (batch.type) {
        case PrimitiveBatch::POINTS:
            // Points are rendered as small quads
        case PrimitiveBatch::TRIANGLES:
        case PrimitiveBatch::QUADS:
            rlBegin(RL_TRIANGLES);
            break;
        case PrimitiveBatch::LINES:
            rlBegin(RL_LINES);
            break;
    }
    
    // Submit vertices
    for (size_t i = 0; i < batch.indices.size(); ++i) {
        const auto& vertex = batch.vertices[batch.indices[i]];
        rlColor4ub(vertex.color.r, vertex.color.g, vertex.color.b, vertex.color.a);
        rlVertex2f(vertex.position.x, vertex.position.y);
    }
    
    rlEnd();
    EndBlendMode();
    
    m_stats.draw_calls_issued++;
    m_stats.batches_flushed++;
    
    // Update triangle count
    if (batch.type == PrimitiveBatch::TRIANGLES || batch.type == PrimitiveBatch::QUADS) {
        m_stats.triangles_rendered += batch.indices.size() / 3;
    }
    
    batch.clear();
}

void PrimitiveRenderer::optimizeBatch(PrimitiveBatch& batch) {
    // Remove degenerate triangles and optimize vertex order
    if (batch.type != PrimitiveBatch::TRIANGLES) {
        return;
    }
    
    std::vector<uint16_t> optimized_indices;
    optimized_indices.reserve(batch.indices.size());
    
    // Process triangles (groups of 3 indices)
    for (size_t i = 0; i + 2 < batch.indices.size(); i += 3) {
        uint16_t i0 = batch.indices[i];
        uint16_t i1 = batch.indices[i + 1];
        uint16_t i2 = batch.indices[i + 2];
        
        // Check for degenerate triangle (same vertices)
        if (i0 != i1 && i1 != i2 && i0 != i2) {
            const auto& v0 = batch.vertices[i0].position;
            const auto& v1 = batch.vertices[i1].position;
            const auto& v2 = batch.vertices[i2].position;
            
            // Check for degenerate triangle (collinear points)
            float area = std::abs((v1.x - v0.x) * (v2.y - v0.y) - (v2.x - v0.x) * (v1.y - v0.y));
            if (area > 0.001f) { // Minimum area threshold
                optimized_indices.insert(optimized_indices.end(), {i0, i1, i2});
            }
        }
    }
    
    batch.indices = std::move(optimized_indices);
}

// Geometry utility methods

Point PrimitiveRenderer::calculateNormal(const Point& p1, const Point& p2) {
    Point direction = {p2.x - p1.x, p2.y - p1.y};
    float length = std::sqrt(direction.x * direction.x + direction.y * direction.y);
    
    if (length < 0.001f) {
        return {0, 0};
    }
    
    // Return perpendicular normal (rotated 90 degrees)
    return {-direction.y / length, direction.x / length};
}

float PrimitiveRenderer::calculateDistance(const Point& p1, const Point& p2) {
    float dx = p2.x - p1.x;
    float dy = p2.y - p1.y;
    return std::sqrt(dx * dx + dy * dy);
}

Point PrimitiveRenderer::interpolatePoints(const Point& p1, const Point& p2, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return {
        p1.x + t * (p2.x - p1.x),
        p1.y + t * (p2.y - p1.y)
    };
}

bool PrimitiveRenderer::isPointInPolygon(const Point& point, const std::vector<Point>& polygon) {
    if (polygon.size() < 3) {
        return false;
    }
    
    // Ray casting algorithm
    bool inside = false;
    size_t j = polygon.size() - 1;
    
    for (size_t i = 0; i < polygon.size(); j = i++) {
        const Point& pi = polygon[i];
        const Point& pj = polygon[j];
        
        if (((pi.y > point.y) != (pj.y > point.y)) &&
            (point.x < (pj.x - pi.x) * (point.y - pi.y) / (pj.y - pi.y) + pi.x)) {
            inside = !inside;
        }
    }
    
    return inside;
}

// PrimitiveGeometry namespace implementation

namespace PrimitiveGeometry {

std::vector<Point> generateCirclePoints(const Point& center, float radius, int segments) {
    std::vector<Point> points;
    points.reserve(segments + 1);
    
    for (int i = 0; i <= segments; ++i) {
        float angle = (2.0f * M_PI * i) / segments;
        points.push_back({
            center.x + radius * std::cos(angle),
            center.y + radius * std::sin(angle)
        });
    }
    
    return points;
}

std::vector<Point> generateArcPoints(const Point& center, float radius, 
                                    float start_angle, float end_angle, int segments) {
    std::vector<Point> points;
    points.reserve(segments + 1);
    
    // Convert to radians
    float start_rad = start_angle * M_PI / 180.0f;
    float end_rad = end_angle * M_PI / 180.0f;
    
    // Ensure positive range
    while (end_rad < start_rad) {
        end_rad += 2.0f * M_PI;
    }
    
    float angle_range = end_rad - start_rad;
    
    for (int i = 0; i <= segments; ++i) {
        float angle = start_rad + (angle_range * i) / segments;
        points.push_back({
            center.x + radius * std::cos(angle),
            center.y + radius * std::sin(angle)
        });
    }
    
    return points;
}

bool isConvexPolygon(const std::vector<Point>& points) {
    if (points.size() < 3) {
        return false;
    }
    
    if (points.size() == 3) {
        return true; // Triangles are always convex
    }
    
    bool sign = false;
    bool first = true;
    
    for (size_t i = 0; i < points.size(); ++i) {
        size_t j = (i + 1) % points.size();
        size_t k = (i + 2) % points.size();
        
        const Point& p1 = points[i];
        const Point& p2 = points[j];
        const Point& p3 = points[k];
        
        // Calculate cross product
        float cross = (p2.x - p1.x) * (p3.y - p1.y) - (p2.y - p1.y) * (p3.x - p1.x);
        
        if (std::abs(cross) > 0.001f) { // Not collinear
            bool current_sign = cross > 0;
            
            if (first) {
                sign = current_sign;
                first = false;
            } else if (sign != current_sign) {
                return false; // Sign change indicates concave
            }
        }
    }
    
    return true;
}

float calculatePolygonArea(const std::vector<Point>& points) {
    if (points.size() < 3) {
        return 0.0f;
    }
    
    float area = 0.0f;
    size_t j = points.size() - 1;
    
    for (size_t i = 0; i < points.size(); j = i++) {
        area += (points[j].x + points[i].x) * (points[j].y - points[i].y);
    }
    
    return std::abs(area) * 0.5f;
}

Point calculatePolygonCentroid(const std::vector<Point>& points) {
    if (points.empty()) {
        return {0, 0};
    }
    
    Point centroid = {0, 0};
    for (const Point& point : points) {
        centroid.x += point.x;
        centroid.y += point.y;
    }
    
    centroid.x /= static_cast<float>(points.size());
    centroid.y /= static_cast<float>(points.size());
    
    return centroid;
}

std::vector<Point> simplifyPolygon(const std::vector<Point>& points, float tolerance) {
    if (points.size() <= 2) {
        return points;
    }
    
    // Douglas-Peucker simplification algorithm
    std::vector<Point> simplified;
    simplified.reserve(points.size());
    
    // Find the point with maximum distance from line between first and last
    float max_distance = 0.0f;
    size_t max_index = 0;
    
    const Point& start = points.front();
    const Point& end = points.back();
    
    for (size_t i = 1; i < points.size() - 1; ++i) {
        float distance = std::abs((end.y - start.y) * points[i].x - 
                                 (end.x - start.x) * points[i].y + 
                                 end.x * start.y - end.y * start.x) /
                        std::sqrt((end.y - start.y) * (end.y - start.y) + 
                                 (end.x - start.x) * (end.x - start.x));
        
        if (distance > max_distance) {
            max_distance = distance;
            max_index = i;
        }
    }
    
    // If max distance is greater than tolerance, recursively simplify
    if (max_distance > tolerance) {
        // Simplify first part
        std::vector<Point> first_part(points.begin(), points.begin() + max_index + 1);
        std::vector<Point> simplified_first = simplifyPolygon(first_part, tolerance);
        
        // Simplify second part
        std::vector<Point> second_part(points.begin() + max_index, points.end());
        std::vector<Point> simplified_second = simplifyPolygon(second_part, tolerance);
        
        // Combine results (remove duplicate point at junction)
        simplified = simplified_first;
        simplified.insert(simplified.end(), simplified_second.begin() + 1, simplified_second.end());
    } else {
        // All points are within tolerance, keep only endpoints
        simplified.push_back(start);
        simplified.push_back(end);
    }
    
    return simplified;
}

std::vector<Point> generateBezierPoints(const Point& start, const Point& control, const Point& end, int segments) {
    std::vector<Point> points;
    points.reserve(segments + 1);
    
    for (int i = 0; i <= segments; ++i) {
        float t = static_cast<float>(i) / segments;
        float u = 1.0f - t;
        
        // Quadratic Bezier formula: B(t) = (1-t)²P₀ + 2(1-t)tP₁ + t²P₂
        Point point = {
            u * u * start.x + 2 * u * t * control.x + t * t * end.x,
            u * u * start.y + 2 * u * t * control.y + t * t * end.y
        };
        
        points.push_back(point);
    }
    
    return points;
}

std::vector<Point> generateBezierPoints(const Point& start, const Point& control1, 
                                       const Point& control2, const Point& end, int segments) {
    std::vector<Point> points;
    points.reserve(segments + 1);
    
    for (int i = 0; i <= segments; ++i) {
        float t = static_cast<float>(i) / segments;
        float u = 1.0f - t;
        
        // Cubic Bezier formula: B(t) = (1-t)³P₀ + 3(1-t)²tP₁ + 3(1-t)t²P₂ + t³P₃
        Point point = {
            u * u * u * start.x + 3 * u * u * t * control1.x + 
            3 * u * t * t * control2.x + t * t * t * end.x,
            u * u * u * start.y + 3 * u * u * t * control1.y + 
            3 * u * t * t * control2.y + t * t * t * end.y
        };
        
        points.push_back(point);
    }
    
    return points;
}

std::vector<Point> generateSplinePoints(const std::vector<Point>& control_points, int segments_per_curve) {
    if (control_points.size() < 2) {
        return control_points;
    }
    
    if (control_points.size() == 2) {
        // Linear interpolation
        std::vector<Point> points;
        points.reserve(segments_per_curve + 1);
        
        for (int i = 0; i <= segments_per_curve; ++i) {
            float t = static_cast<float>(i) / segments_per_curve;
            Point point = {
                control_points[0].x + t * (control_points[1].x - control_points[0].x),
                control_points[0].y + t * (control_points[1].y - control_points[0].y)
            };
            points.push_back(point);
        }
        
        return points;
    }
    
    // Catmull-Rom spline
    std::vector<Point> spline_points;
    spline_points.reserve((control_points.size() - 1) * segments_per_curve + 1);
    
    for (size_t i = 0; i < control_points.size() - 1; ++i) {
        Point p0 = (i == 0) ? control_points[0] : control_points[i - 1];
        Point p1 = control_points[i];
        Point p2 = control_points[i + 1];
        Point p3 = (i + 2 >= control_points.size()) ? control_points.back() : control_points[i + 2];
        
        for (int j = 0; j < segments_per_curve; ++j) {
            float t = static_cast<float>(j) / segments_per_curve;
            float t2 = t * t;
            float t3 = t2 * t;
            
            // Catmull-Rom formula
            Point point = {
                0.5f * ((2 * p1.x) + (-p0.x + p2.x) * t + 
                       (2 * p0.x - 5 * p1.x + 4 * p2.x - p3.x) * t2 +
                       (-p0.x + 3 * p1.x - 3 * p2.x + p3.x) * t3),
                0.5f * ((2 * p1.y) + (-p0.y + p2.y) * t + 
                       (2 * p0.y - 5 * p1.y + 4 * p2.y - p3.y) * t2 +
                       (-p0.y + 3 * p1.y - 3 * p2.y + p3.y) * t3)
            };
            
            spline_points.push_back(point);
        }
    }
    
    // Add final point
    spline_points.push_back(control_points.back());
    
    return spline_points;
}

Point getLineIntersection(const Point& line1_start, const Point& line1_end,
                         const Point& line2_start, const Point& line2_end) {
    float x1 = line1_start.x, y1 = line1_start.y;
    float x2 = line1_end.x, y2 = line1_end.y;
    float x3 = line2_start.x, y3 = line2_start.y;
    float x4 = line2_end.x, y4 = line2_end.y;
    
    float denom = (x1 - x2) * (y3 - y4) - (y1 - y2) * (x3 - x4);
    
    if (std::abs(denom) < 0.001f) {
        // Lines are parallel
        return {NAN, NAN};
    }
    
    float t = ((x1 - x3) * (y3 - y4) - (y1 - y3) * (x3 - x4)) / denom;
    
    return {
        x1 + t * (x2 - x1),
        y1 + t * (y2 - y1)
    };
}

float getDistanceToLine(const Point& point, const Point& line_start, const Point& line_end) {
    float dx = line_end.x - line_start.x;
    float dy = line_end.y - line_start.y;
    
    if (dx == 0 && dy == 0) {
        // Line is a point
        dx = point.x - line_start.x;
        dy = point.y - line_start.y;
        return std::sqrt(dx * dx + dy * dy);
    }
    
    float t = ((point.x - line_start.x) * dx + (point.y - line_start.y) * dy) / (dx * dx + dy * dy);
    t = std::clamp(t, 0.0f, 1.0f);
    
    Point closest = {
        line_start.x + t * dx,
        line_start.y + t * dy
    };
    
    dx = point.x - closest.x;
    dy = point.y - closest.y;
    
    return std::sqrt(dx * dx + dy * dy);
}

Point getClosestPointOnLine(const Point& point, const Point& line_start, const Point& line_end) {
    float dx = line_end.x - line_start.x;
    float dy = line_end.y - line_start.y;
    
    if (dx == 0 && dy == 0) {
        return line_start;
    }
    
    float t = ((point.x - line_start.x) * dx + (point.y - line_start.y) * dy) / (dx * dx + dy * dy);
    t = std::clamp(t, 0.0f, 1.0f);
    
    return {
        line_start.x + t * dx,
        line_start.y + t * dy
    };
}

} // namespace PrimitiveGeometry

} // namespace Kairos// KairosServer/src/Graphics/PrimitiveRenderer.cpp
#include <Graphics/PrimitiveRenderer.hpp>
#include <Utils/Logger.hpp>
#include <cmath>
#include