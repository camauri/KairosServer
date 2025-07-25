// KairosServer/include/Graphics/TextRenderer.hpp
#pragma once

#include <Types.hpp>
#include <raylib.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

namespace Kairos {

class FontManager;

/**
 * @brief Specialized text rendering with font atlas optimization
 */
class TextRenderer {
public:
    struct TextMetrics {
        float width;
        float height;
        float baseline;
        float line_height;
    };
    
    struct GlyphInfo {
        uint32_t codepoint;
        Rectangle source_rect;
        Point offset;
        float advance;
    };
    
    struct TextBatch {
        uint32_t font_id;
        float font_size;
        Color color;
        std::vector<TexturedVertex> vertices;
        
        void clear() { vertices.clear(); }
        bool isEmpty() const { return vertices.empty(); }
    };

public:
    explicit TextRenderer(FontManager& font_manager);
    ~TextRenderer();
    
    // Initialization
    bool initialize();
    void shutdown();
    
    // Text rendering
    void drawText(const std::string& text, const Point& position, 
                  uint32_t font_id, float font_size, const Color& color);
    
    void drawTextCentered(const std::string& text, const Point& center,
                         uint32_t font_id, float font_size, const Color& color);
    
    void drawTextAligned(const std::string& text, const Rectangle& bounds,
                        uint32_t font_id, float font_size, const Color& color,
                        int horizontal_align, int vertical_align);
    
    // Text measurement
    TextMetrics measureText(const std::string& text, uint32_t font_id, float font_size) const;
    float getTextWidth(const std::string& text, uint32_t font_id, float font_size) const;
    float getTextHeight(uint32_t font_id, float font_size) const;
    
    // Batching
    void beginBatch();
    void endBatch();
    void flushBatches();
    
    // Font atlas management
    bool rebuildFontAtlas(uint32_t font_id);
    void optimizeFontAtlas();
    
    // Configuration
    void setPixelPerfect(bool enabled) { m_pixel_perfect = enabled; }
    void setKerningEnabled(bool enabled) { m_kerning_enabled = enabled; }
    void setSubpixelPositioning(bool enabled) { m_subpixel_positioning = enabled; }
    
    // Statistics
    struct Stats {
        uint64_t characters_rendered = 0;
        uint64_t draw_calls_issued = 0;
        uint64_t batches_flushed = 0;
        uint32_t active_font_atlases = 0;
        size_t atlas_memory_usage = 0;
    };
    
    const Stats& getStats() const { return m_stats; }
    void resetStats();

private:
    // Internal text processing
    std::vector<uint32_t> utf8ToCodepoints(const std::string& text) const;
    GlyphInfo getGlyphInfo(uint32_t font_id, uint32_t codepoint, float font_size) const;
    float getKerning(uint32_t font_id, uint32_t previous, uint32_t current, float font_size) const;
    
    // Vertex generation
    void generateTextVertices(const std::string& text, const Point& position,
                             uint32_t font_id, float font_size, const Color& color,
                             std::vector<TexturedVertex>& vertices) const;
    
    // Batching system
    void addToBatch(uint32_t font_id, float font_size, const Color& color,
                   const std::vector<TexturedVertex>& vertices);
    void flushBatch(TextBatch& batch);
    
    // Font atlas utilities
    bool ensureFontAtlas(uint32_t font_id, float font_size);
    Rectangle packGlyph(uint32_t font_id, uint32_t codepoint, float font_size);

private:
    FontManager& m_font_manager;
    
    // Configuration
    bool m_pixel_perfect = false;
    bool m_kerning_enabled = true;
    bool m_subpixel_positioning = true;
    bool m_batching_enabled = true;
    
    // Batching state
    bool m_in_batch = false;
    std::vector<TextBatch> m_text_batches;
    
    // Font atlas cache
    struct FontAtlasInfo {
        uint32_t texture_id;
        uint32_t atlas_width;
        uint32_t atlas_height;
        std::unordered_map<uint32_t, GlyphInfo> glyph_cache;
        bool needs_rebuild = false;
    };
    
    std::unordered_map<uint32_t, FontAtlasInfo> m_font_atlases;
    
    // Statistics
    Stats m_stats;
};

/**
 * @brief Text layout and formatting utilities
 */
class TextLayout {
public:
    enum class HorizontalAlign {
        LEFT,
        CENTER,
        RIGHT,
        JUSTIFY
    };
    
    enum class VerticalAlign {
        TOP,
        MIDDLE,
        BOTTOM,
        BASELINE
    };
    
    struct LayoutOptions {
        HorizontalAlign horizontal_align = HorizontalAlign::LEFT;
        VerticalAlign vertical_align = VerticalAlign::TOP;
        float line_spacing = 1.0f;
        float word_spacing = 1.0f;
        bool word_wrap = false;
        float wrap_width = 0.0f;
    };
    
    struct LayoutResult {
        std::vector<Point> character_positions;
        std::vector<uint32_t> line_breaks;
        Rectangle bounds;
        uint32_t line_count;
    };

public:
    static LayoutResult layoutText(const std::string& text, const Rectangle& bounds,
                                  uint32_t font_id, float font_size,
                                  const LayoutOptions& options, const TextRenderer& renderer);
    
    static std::vector<std::string> wrapText(const std::string& text, float max_width,
                                            uint32_t font_id, float font_size,
                                            const TextRenderer& renderer);
    
    static Point alignText(const Rectangle& bounds, const Rectangle& text_bounds,
                          HorizontalAlign horizontal, VerticalAlign vertical);
};

} // namespace Kairos