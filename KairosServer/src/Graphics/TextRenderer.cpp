// KairosServer/src/Graphics/TextRenderer.cpp
#include <Graphics/TextRenderer.hpp>
#include <Core/FontManager.hpp>
#include <Utils/Logger.hpp>
#include <algorithm>
#include <cmath>

namespace Kairos {

TextRenderer::TextRenderer(FontManager& font_manager) 
    : m_font_manager(font_manager) {
    
    Logger::debug("TextRenderer created");
}

TextRenderer::~TextRenderer() {
    if (m_text_batches.size() > 0) {
        Logger::warning("TextRenderer destroyed with {} pending batches", m_text_batches.size());
    }
}

bool TextRenderer::initialize() {
    Logger::info("Initializing TextRenderer...");
    
    // Reserve space for text batches
    m_text_batches.reserve(64);
    
    Logger::info("TextRenderer initialized successfully");
    return true;
}

void TextRenderer::shutdown() {
    Logger::info("Shutting down TextRenderer...");
    
    // Clear all batches
    m_text_batches.clear();
    
    // Clear font atlas cache
    m_font_atlases.clear();
    
    Logger::info("TextRenderer shutdown complete");
}

void TextRenderer::drawText(const std::string& text, const Point& position, 
                           uint32_t font_id, float font_size, const Color& color) {
    if (text.empty()) {
        return;
    }
    
    // Generate vertices for text
    std::vector<TexturedVertex> vertices;
    generateTextVertices(text, position, font_id, font_size, color, vertices);
    
    if (!vertices.empty()) {
        if (m_batching_enabled && m_in_batch) {
            addToBatch(font_id, font_size, color, vertices);
        } else {
            // Draw immediately
            TextBatch immediate_batch;
            immediate_batch.font_id = font_id;
            immediate_batch.font_size = font_size;
            immediate_batch.color = color;
            immediate_batch.vertices = std::move(vertices);
            flushBatch(immediate_batch);
        }
        
        m_stats.characters_rendered += text.length();
    }
}

void TextRenderer::drawTextCentered(const std::string& text, const Point& center,
                                   uint32_t font_id, float font_size, const Color& color) {
    TextMetrics metrics = measureText(text, font_id, font_size);
    Point position = {
        center.x - metrics.width / 2.0f,
        center.y - metrics.height / 2.0f
    };
    
    drawText(text, position, font_id, font_size, color);
}

void TextRenderer::drawTextAligned(const std::string& text, const Rectangle& bounds,
                                  uint32_t font_id, float font_size, const Color& color,
                                  int horizontal_align, int vertical_align) {
    TextMetrics metrics = measureText(text, font_id, font_size);
    
    Point position = bounds.position;
    
    // Horizontal alignment
    switch (horizontal_align) {
        case 1: // CENTER
            position.x += (bounds.width - metrics.width) / 2.0f;
            break;
        case 2: // RIGHT
            position.x += bounds.width - metrics.width;
            break;
        default: // LEFT
            break;
    }
    
    // Vertical alignment
    switch (vertical_align) {
        case 1: // MIDDLE
            position.y += (bounds.height - metrics.height) / 2.0f;
            break;
        case 2: // BOTTOM
            position.y += bounds.height - metrics.height;
            break;
        default: // TOP
            break;
    }
    
    drawText(text, position, font_id, font_size, color);
}

TextRenderer::TextMetrics TextRenderer::measureText(const std::string& text, uint32_t font_id, float font_size) const {
    TextMetrics metrics = {0, 0, 0, 0};
    
    if (text.empty()) {
        return metrics;
    }
    
    const FontManager::FontData* font_data = m_font_manager.getFont(font_id);
    if (!font_data) {
        Logger::warning("Font {} not found for text measurement", font_id);
        return metrics;
    }
    
    // Convert UTF-8 to codepoints
    std::vector<uint32_t> codepoints = utf8ToCodepoints(text);
    
    float scale = font_size / static_cast<float>(font_data->font_size);
    float total_width = 0.0f;
    float max_height = 0.0f;
    
    for (size_t i = 0; i < codepoints.size(); ++i) {
        GlyphInfo glyph = getGlyphInfo(font_id, codepoints[i], font_size);
        
        total_width += glyph.advance * scale;
        max_height = std::max(max_height, glyph.source_rect.height * scale);
        
        // Add kerning if not the last character
        if (i < codepoints.size() - 1 && m_kerning_enabled) {
            float kerning = getKerning(font_id, codepoints[i], codepoints[i + 1], font_size);
            total_width += kerning;
        }
    }
    
    metrics.width = total_width;
    metrics.height = max_height;
    metrics.baseline = max_height * 0.8f; // Approximate baseline
    metrics.line_height = max_height * 1.2f; // Approximate line height
    
    return metrics;
}

float TextRenderer::getTextWidth(const std::string& text, uint32_t font_id, float font_size) const {
    return measureText(text, font_id, font_size).width;
}

float TextRenderer::getTextHeight(uint32_t font_id, float font_size) const {
    const FontManager::FontData* font_data = m_font_manager.getFont(font_id);
    if (!font_data) {
        return font_size; // Fallback
    }
    
    float scale = font_size / static_cast<float>(font_data->font_size);
    return static_cast<float>(font_data->raylib_font.baseSize) * scale;
}

void TextRenderer::beginBatch() {
    m_in_batch = true;
    m_text_batches.clear();
}

void TextRenderer::endBatch() {
    flushBatches();
    m_in_batch = false;
}

void TextRenderer::flushBatches() {
    for (auto& batch : m_text_batches) {
        if (!batch.isEmpty()) {
            flushBatch(batch);
        }
    }
    
    m_text_batches.clear();
    m_stats.batches_flushed++;
}

bool TextRenderer::rebuildFontAtlas(uint32_t font_id) {
    auto it = m_font_atlases.find(font_id);
    if (it != m_font_atlases.end()) {
        it->second.needs_rebuild = true;
        it->second.glyph_cache.clear();
        Logger::debug("Marked font atlas {} for rebuild", font_id);
        return true;
    }
    
    return false;
}

void TextRenderer::optimizeFontAtlas() {
    // Rebuild atlases that need it
    for (auto& [font_id, atlas] : m_font_atlases) {
        if (atlas.needs_rebuild) {
            // In a real implementation, this would rebuild the atlas texture
            atlas.needs_rebuild = false;
            Logger::debug("Rebuilt font atlas for font {}", font_id);
        }
    }
}

void TextRenderer::resetStats() {
    m_stats = Stats{};
    Logger::debug("TextRenderer statistics reset");
}

// Private methods implementation

std::vector<uint32_t> TextRenderer::utf8ToCodepoints(const std::string& text) const {
    std::vector<uint32_t> codepoints;
    codepoints.reserve(text.length()); // Conservative estimate
    
    for (size_t i = 0; i < text.length(); ) {
        uint32_t codepoint = 0;
        uint8_t byte = static_cast<uint8_t>(text[i]);
        
        if (byte < 0x80) {
            // ASCII character
            codepoint = byte;
            i++;
        } else if ((byte & 0xE0) == 0xC0) {
            // 2-byte UTF-8
            if (i + 1 < text.length()) {
                codepoint = ((byte & 0x1F) << 6) | (static_cast<uint8_t>(text[i + 1]) & 0x3F);
                i += 2;
            } else {
                break;
            }
        } else if ((byte & 0xF0) == 0xE0) {
            // 3-byte UTF-8
            if (i + 2 < text.length()) {
                codepoint = ((byte & 0x0F) << 12) | 
                           ((static_cast<uint8_t>(text[i + 1]) & 0x3F) << 6) |
                           (static_cast<uint8_t>(text[i + 2]) & 0x3F);
                i += 3;
            } else {
                break;
            }
        } else if ((byte & 0xF8) == 0xF0) {
            // 4-byte UTF-8
            if (i + 3 < text.length()) {
                codepoint = ((byte & 0x07) << 18) |
                           ((static_cast<uint8_t>(text[i + 1]) & 0x3F) << 12) |
                           ((static_cast<uint8_t>(text[i + 2]) & 0x3F) << 6) |
                           (static_cast<uint8_t>(text[i + 3]) & 0x3F);
                i += 4;
            } else {
                break;
            }
        } else {
            // Invalid UTF-8, skip byte
            i++;
            continue;
        }
        
        codepoints.push_back(codepoint);
    }
    
    return codepoints;
}

TextRenderer::GlyphInfo TextRenderer::getGlyphInfo(uint32_t font_id, uint32_t codepoint, float font_size) const {
    GlyphInfo glyph_info = {};
    
    const FontManager::FontData* font_data = m_font_manager.getFont(font_id);
    if (!font_data) {
        return glyph_info;
    }
    
    // Check font atlas cache
    auto atlas_it = m_font_atlases.find(font_id);
    if (atlas_it != m_font_atlases.end()) {
        auto glyph_it = atlas_it->second.glyph_cache.find(codepoint);
        if (glyph_it != atlas_it->second.glyph_cache.end()) {
            return glyph_it->second;
        }
    }
    
    // Get glyph from Raylib font
    const Font& raylib_font = font_data->raylib_font;
    
    // Find glyph in font
    int glyph_index = 0;
    for (int i = 0; i < raylib_font.glyphCount; ++i) {
        if (raylib_font.glyphs[i].value == static_cast<int>(codepoint)) {
            glyph_index = i;
            break;
        }
    }
    
    if (glyph_index < raylib_font.glyphCount) {
        const GlyphInfo& font_glyph = raylib_font.glyphs[glyph_index];
        
        glyph_info.codepoint = codepoint;
        glyph_info.source_rect = {
            static_cast<float>(font_glyph.offsetX),
            static_cast<float>(font_glyph.offsetY),
            static_cast<float>(font_glyph.image.width),
            static_cast<float>(font_glyph.image.height)
        };
        glyph_info.offset = {
            static_cast<float>(font_glyph.offsetX),
            static_cast<float>(font_glyph.offsetY)
        };
        glyph_info.advance = static_cast<float>(font_glyph.advanceX);
    }
    
    return glyph_info;
}

float TextRenderer::getKerning(uint32_t font_id, uint32_t previous, uint32_t current, float font_size) const {
    // Simplified kerning - in a real implementation, this would use font kerning tables
    return 0.0f;
}

void TextRenderer::generateTextVertices(const std::string& text, const Point& position,
                                       uint32_t font_id, float font_size, const Color& color,
                                       std::vector<TexturedVertex>& vertices) const {
    if (text.empty()) {
        return;
    }
    
    std::vector<uint32_t> codepoints = utf8ToCodepoints(text);
    vertices.reserve(codepoints.size() * 6); // 2 triangles per character
    
    const FontManager::FontData* font_data = m_font_manager.getFont(font_id);
    if (!font_data) {
        return;
    }
    
    float scale = font_size / static_cast<float>(font_data->font_size);
    float current_x = position.x;
    float current_y = position.y;
    
    for (size_t i = 0; i < codepoints.size(); ++i) {
        GlyphInfo glyph = getGlyphInfo(font_id, codepoints[i], font_size);
        
        if (glyph.source_rect.width > 0 && glyph.source_rect.height > 0) {
            // Calculate glyph position
            float glyph_x = current_x + glyph.offset.x * scale;
            float glyph_y = current_y + glyph.offset.y * scale;
            float glyph_w = glyph.source_rect.width * scale;
            float glyph_h = glyph.source_rect.height * scale;
            
            // Calculate texture coordinates (normalized)
            float tex_x1 = glyph.source_rect.x / static_cast<float>(font_data->raylib_font.texture.width);
            float tex_y1 = glyph.source_rect.y / static_cast<float>(font_data->raylib_font.texture.height);
            float tex_x2 = tex_x1 + glyph.source_rect.width / static_cast<float>(font_data->raylib_font.texture.width);
            float tex_y2 = tex_y1 + glyph.source_rect.height / static_cast<float>(font_data->raylib_font.texture.height);
            
            // Generate quad vertices (2 triangles)
            uint32_t color_rgba = color.rgba;
            
            // Triangle 1
            vertices.push_back({glyph_x, glyph_y, tex_x1, tex_y1, color_rgba});
            vertices.push_back({glyph_x + glyph_w, glyph_y, tex_x2, tex_y1, color_rgba});
            vertices.push_back({glyph_x, glyph_y + glyph_h, tex_x1, tex_y2, color_rgba});
            
            // Triangle 2
            vertices.push_back({glyph_x + glyph_w, glyph_y, tex_x2, tex_y1, color_rgba});
            vertices.push_back({glyph_x + glyph_w, glyph_y + glyph_h, tex_x2, tex_y2, color_rgba});
            vertices.push_back({glyph_x, glyph_y + glyph_h, tex_x1, tex_y2, color_rgba});
        }
        
        // Advance position
        current_x += glyph.advance * scale;
        
        // Add kerning
        if (i < codepoints.size() - 1 && m_kerning_enabled) {
            float kerning = getKerning(font_id, codepoints[i], codepoints[i + 1], font_size);
            current_x += kerning;
        }
        
        // Apply pixel perfect positioning if enabled
        if (m_pixel_perfect) {
            current_x = std::round(current_x);
        }
    }
}

void TextRenderer::addToBatch(uint32_t font_id, float font_size, const Color& color,
                             const std::vector<TexturedVertex>& vertices) {
    // Find existing batch or create new one
    TextBatch* target_batch = nullptr;
    
    for (auto& batch : m_text_batches) {
        if (batch.font_id == font_id && 
            std::abs(batch.font_size - font_size) < 0.01f &&
            batch.color.rgba == color.rgba) {
            target_batch = &batch;
            break;
        }
    }
    
    if (!target_batch) {
        m_text_batches.emplace_back();
        target_batch = &m_text_batches.back();
        target_batch->font_id = font_id;
        target_batch->font_size = font_size;
        target_batch->color = color;
    }
    
    // Add vertices to batch
    target_batch->vertices.insert(target_batch->vertices.end(), vertices.begin(), vertices.end());
}

void TextRenderer::flushBatch(TextBatch& batch) {
    if (batch.isEmpty()) {
        return;
    }
    
    // Get font texture
    const FontManager::FontData* font_data = m_font_manager.getFont(batch.font_id);
    if (!font_data || font_data->raylib_font.texture.id == 0) {
        Logger::warning("Invalid font texture for batch flush");
        return;
    }
    
    // In a real implementation, this would render the vertices using the font texture
    // For now, we'll use Raylib's direct rendering as a placeholder
    
    // Draw vertices using Raylib
    rlSetTexture(font_data->raylib_font.texture.id);
    rlBegin(RL_TRIANGLES);
    
    for (const auto& vertex : batch.vertices) {
        rlColor4ub(batch.color.r, batch.color.g, batch.color.b, batch.color.a);
        rlTexCoord2f(vertex.u, vertex.v);
        rlVertex2f(vertex.x, vertex.y);
    }
    
    rlEnd();
    rlSetTexture(0);
    
    m_stats.draw_calls_issued++;
    batch.clear();
}

bool TextRenderer::ensureFontAtlas(uint32_t font_id, float font_size) {
    auto it = m_font_atlases.find(font_id);
    if (it != m_font_atlases.end() && !it->second.needs_rebuild) {
        return true; // Atlas already exists and is valid
    }
    
    // Create or rebuild font atlas
    FontAtlasInfo atlas;
    atlas.texture_id = font_id; // Use font ID as texture ID for simplicity
    atlas.atlas_width = 512;     // Default atlas size
    atlas.atlas_height = 512;
    atlas.needs_rebuild = false;
    
    m_font_atlases[font_id] = atlas;
    m_stats.active_font_atlases++;
    
    Logger::debug("Created font atlas for font {}", font_id);
    return true;
}

Rectangle TextRenderer::packGlyph(uint32_t font_id, uint32_t codepoint, float font_size) {
    // Simplified glyph packing - in a real implementation, this would use
    // a proper texture atlas packing algorithm
    return {0, 0, 16, 16}; // Placeholder
}

// TextLayout implementation

TextLayout::LayoutResult TextLayout::layoutText(const std::string& text, const Rectangle& bounds,
                                               uint32_t font_id, float font_size,
                                               const LayoutOptions& options, const TextRenderer& renderer) {
    LayoutResult result;
    
    if (text.empty()) {
        result.bounds = {bounds.x, bounds.y, 0, 0};
        result.line_count = 0;
        return result;
    }
    
    // For now, implement simple single-line layout
    TextRenderer::TextMetrics metrics = renderer.measureText(text, font_id, font_size);
    
    result.character_positions.reserve(text.length());
    
    float current_x = bounds.x;
    float current_y = bounds.y;
    
    // Apply horizontal alignment
    switch (options.horizontal_align) {
        case HorizontalAlign::CENTER:
            current_x += (bounds.width - metrics.width) / 2.0f;
            break;
        case HorizontalAlign::RIGHT:
            current_x += bounds.width - metrics.width;
            break;
        default:
            break;
    }
    
    // Apply vertical alignment
    switch (options.vertical_align) {
        case VerticalAlign::MIDDLE:
            current_y += (bounds.height - metrics.height) / 2.0f;
            break;
        case VerticalAlign::BOTTOM:
            current_y += bounds.height - metrics.height;
            break;
        default:
            break;
    }
    
    // Generate character positions
    for (size_t i = 0; i < text.length(); ++i) {
        result.character_positions.push_back({current_x, current_y});
        // For simplicity, assume average character width
        current_x += metrics.width / static_cast<float>(text.length());
    }
    
    result.bounds = {bounds.x, bounds.y, metrics.width, metrics.height};
    result.line_count = 1;
    
    return result;
}

std::vector<std::string> TextLayout::wrapText(const std::string& text, float max_width,
                                             uint32_t font_id, float font_size,
                                             const TextRenderer& renderer) {
    std::vector<std::string> lines;
    
    if (text.empty() || max_width <= 0) {
        return lines;
    }
    
    // Simple word wrapping implementation
    std::string current_line;
    std::string current_word;
    
    for (char c : text) {
        if (c == ' ' || c == '\n') {
            // Check if adding the word would exceed max width
            std::string test_line = current_line.empty() ? current_word : current_line + " " + current_word;
            float width = renderer.getTextWidth(test_line, font_id, font_size);
            
            if (width <= max_width || current_line.empty()) {
                current_line = test_line;
            } else {
                // Start new line
                lines.push_back(current_line);
                current_line = current_word;
            }
            
            current_word.clear();
            
            if (c == '\n') {
                lines.push_back(current_line);
                current_line.clear();
            }
        } else {
            current_word += c;
        }
    }
    
    // Add remaining text
    if (!current_word.empty()) {
        std::string test_line = current_line.empty() ? current_word : current_line + " " + current_word;
        float width = renderer.getTextWidth(test_line, font_id, font_size);
        
        if (width <= max_width || current_line.empty()) {
            current_line = test_line;
        } else {
            lines.push_back(current_line);
            current_line = current_word;
        }
    }
    
    if (!current_line.empty()) {
        lines.push_back(current_line);
    }
    
    return lines;
}

Point TextLayout::alignText(const Rectangle& bounds, const Rectangle& text_bounds,
                           HorizontalAlign horizontal, VerticalAlign vertical) {
    Point position = {bounds.x, bounds.y};
    
    // Horizontal alignment
    switch (horizontal) {
        case HorizontalAlign::CENTER:
            position.x += (bounds.width - text_bounds.width) / 2.0f;
            break;
        case HorizontalAlign::RIGHT:
            position.x += bounds.width - text_bounds.width;
            break;
        default:
            break;
    }
    
    // Vertical alignment
    switch (vertical) {
        case VerticalAlign::MIDDLE:
            position.y += (bounds.height - text_bounds.height) / 2.0f;
            break;
        case VerticalAlign::BOTTOM:
            position.y += bounds.height - text_bounds.height;
            break;
        default:
            break;
    }
    
    return position;
}

} // namespace Kairos