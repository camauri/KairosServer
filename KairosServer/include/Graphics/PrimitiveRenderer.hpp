// KairosServer/include/Graphics/PrimitiveRenderer.hpp
#pragma once

#include <Types.hpp>
#include <raylib.h>
#include <vector>
#include <memory>

namespace Kairos {

/**
 * @brief High-performance primitive rendering with batching
 */
class PrimitiveRenderer {
public:
    struct LineVertex {
        Point position;
        Color color;
        float thickness;
    };
    
    struct TriangleVertex {
        Point position;
        Color color;
    };
    
    struct PrimitiveBatch {
        enum Type {
            POINTS,
            LINES,
            TRIANGLES,
            QUADS
        };
        
        Type type;
        std::vector<TriangleVertex> vertices;
        std::vector<uint16_t> indices;
        uint8_t layer_id;
        
        void clear() {
            vertices.clear();
            indices.clear();
        }
        
        bool isEmpty() const {
            return vertices.empty();
        }
    };

public:
    PrimitiveRenderer();
    ~PrimitiveRenderer();
    
    // Initialization
    bool initialize();
    void shutdown();
    
    // Point primitives
    void drawPoint(const Point& position, const Color& color, float size = 1.0f);
    void drawPoints(const std::vector<Point>& points, const Color& color, float size = 1.0f);
    void drawPoints(const std::vector<Point>& points, const std::vector<Color>& colors, float size = 1.0f);
    
    // Line primitives
    void drawLine(const Point& start, const Point& end, const Color& color, float thickness = 1.0f);
    void drawLines(const std::vector<Point>& points, const Color& color, float thickness = 1.0f);
    void drawLineStrip(const std::vector<Point>& points, const Color& color, float thickness = 1.0f);
    void drawLineLoop(const std::vector<Point>& points, const Color& color, float thickness = 1.0f);
    
    // Rectangle primitives
    void drawRectangle(const Point& position, float width, float height, const Color& color, bool filled = true);
    void drawRectangle(const Rectangle& rect, const Color& color, bool filled = true);
    void drawRectangleGradient(const Rectangle& rect, const Color& top_left, const Color& top_right,
                              const Color& bottom_left, const Color& bottom_right);
    void drawRectangleRounded(const Rectangle& rect, float radius, const Color& color, bool filled = true);
    
    // Circle primitives
    void drawCircle(const Point& center, float radius, const Color& color, bool filled = true);
    void drawCircleGradient(const Point& center, float radius, const Color& inner, const Color& outer);
    void drawEllipse(const Point& center, float radius_x, float radius_y, const Color& color, bool filled = true);
    void drawArc(const Point& center, float radius, float start_angle, float end_angle, 
                const Color& color, bool filled = true);
    
    // Polygon primitives
    void drawTriangle(const Point& p1, const Point& p2, const Point& p3, const Color& color, bool filled = true);
    void drawPolygon(const std::vector<Point>& points, const Color& color, bool filled = true);
    void drawPolygonGradient(const std::vector<Point>& points, const std::vector<Color>& colors, bool filled = true);
    
    // Advanced shapes
    void drawBezierQuadratic(const Point& start, const Point& control, const Point& end, 
                            const Color& color, float thickness = 1.0f, int segments = 20);
    void drawBezierCubic(const Point& start, const Point& control1, const Point& control2, const Point& end,
                        const Color& color, float thickness = 1.0f, int segments = 20);
    void drawSpline(const std::vector<Point>& points, const Color& color, float thickness = 1.0f);
    
    // Batching control
    void beginBatch(uint8_t layer_id = 0);
    void endBatch();
    void flushBatches();
    void flushLayer(uint8_t layer_id);
    
    // Rendering state
    void setAntialiasing(bool enabled);
    void setLineJoinStyle(int join_style); // MITER, ROUND, BEVEL
    void setLineCapStyle(int cap_style);   // BUTT, ROUND, SQUARE
    void setBlendMode(int blend_mode);
    
    // Statistics
    struct Stats {
        uint64_t points_rendered = 0;
        uint64_t lines_rendered = 0;
        uint64_t triangles_rendered = 0;
        uint64_t draw_calls_issued = 0;
        uint64_t vertices_processed = 0;
        uint64_t batches_flushed = 0;
    };
    
    const Stats& getStats() const { return m_stats; }
    void resetStats();

private:
    // Internal primitive generation
    void generateLineVertices(const Point& start, const Point& end, float thickness, 
                             const Color& color, std::vector<TriangleVertex>& vertices);
    void generateCircleVertices(const Point& center, float radius, const Color& color,
                               bool filled, int segments, std::vector<TriangleVertex>& vertices);
    void generateRectangleVertices(const Rectangle& rect, const Color& color,
                                  std::vector<TriangleVertex>& vertices);
    void generatePolygonVertices(const std::vector<Point>& points, const Color& color,
                                bool filled, std::vector<TriangleVertex>& vertices);
    
    // Tessellation
    std::vector<uint16_t> triangulatePolygon(const std::vector<Point>& points);
    void tessellateComplexPolygon(const std::vector<Point>& points, 
                                 std::vector<TriangleVertex>& vertices,
                                 std::vector<uint16_t>& indices);
    
    // Batching system
    void addToBatch(PrimitiveBatch::Type type, const std::vector<TriangleVertex>& vertices,
                   const std::vector<uint16_t>& indices, uint8_t layer_id);
    void flushBatch(PrimitiveBatch& batch);
    void optimizeBatch(PrimitiveBatch& batch);
    
    // Geometry utilities
    Point calculateNormal(const Point& p1, const Point& p2);
    float calculateDistance(const Point& p1, const Point& p2);
    Point interpolatePoints(const Point& p1, const Point& p2, float t);
    bool isPointInPolygon(const Point& point, const std::vector<Point>& polygon);

private:
    // Rendering state
    bool m_antialiasing_enabled = true;
    int m_line_join_style = 0; // MITER
    int m_line_cap_style = 0;  // BUTT
    int m_blend_mode = 0;      // ALPHA
    
    // Batching state
    bool m_in_batch = false;
    uint8_t m_current_layer = 0;
    std::vector<PrimitiveBatch> m_primitive_batches;
    
    // Vertex buffers
    std::vector<TriangleVertex> m_vertex_buffer;
    std::vector<uint16_t> m_index_buffer;
    
    // Configuration
    size_t m_max_vertices_per_batch = 10000;
    size_t m_max_indices_per_batch = 15000;
    
    // Statistics
    Stats m_stats;
};

/**
 * @brief Geometry utilities for primitive rendering
 */
namespace PrimitiveGeometry {
    // Circle/arc generation
    std::vector<Point> generateCirclePoints(const Point& center, float radius, int segments);
    std::vector<Point> generateArcPoints(const Point& center, float radius, 
                                        float start_angle, float end_angle, int segments);
    
    // Polygon utilities
    bool isConvexPolygon(const std::vector<Point>& points);
    float calculatePolygonArea(const std::vector<Point>& points);
    Point calculatePolygonCentroid(const std::vector<Point>& points);
    std::vector<Point> simplifyPolygon(const std::vector<Point>& points, float tolerance);
    
    // Curve generation
    std::vector<Point> generateBezierPoints(const Point& start, const Point& control, const Point& end, int segments);
    std::vector<Point> generateBezierPoints(const Point& start, const Point& control1, 
                                           const Point& control2, const Point& end, int segments);
    std::vector<Point> generateSplinePoints(const std::vector<Point>& control_points, int segments_per_curve);
    
    // Line utilities
    Point getLineIntersection(const Point& line1_start, const Point& line1_end,
                             const Point& line2_start, const Point& line2_end);
    float getDistanceToLine(const Point& point, const Point& line_start, const Point& line_end);
    Point getClosestPointOnLine(const Point& point, const Point& line_start, const Point& line_end);
}

} // namespace Kairos