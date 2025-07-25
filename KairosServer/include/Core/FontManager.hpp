// KairosServer/include/Core/FontManager.hpp
#pragma once

#include <raylib.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <chrono>

namespace Kairos {

/**
 * @brief Manages font loading, caching, and text rendering resources
 */
class FontManager {
public:
    struct FontMetadata {
        std::string family_name;
        std::string style_name;
        bool is_monospace = false;
        bool has_kerning = false;
    };
    
    struct FontData {
        uint32_t id;
        std::string file_path;
        uint32_t font_size;
        bool loaded_from_memory = false;
        
        Font raylib_font = {0};
        FontMetadata metadata;
        std::vector<int> custom_codepoints;
        
        size_t memory_usage = 0;
        uint32_t usage_count = 0;
        std::chrono::steady_clock::time_point load_time;
        std::chrono::steady_clock::time_point last_used;
    };
    
    struct FontInfo {
        uint32_t id;
        std::string family_name;
        std::string style_name;
        std::string file_path;
        uint32_t font_size;
        size_t memory_usage;
        uint32_t glyph_count;
        bool is_default;
    };
    
    struct Stats {
        size_t loaded_fonts = 0;
        size_t available_system_fonts = 0;
        uint32_t default_font_id = 0;
        size_t total_memory_usage = 0;
        size_t total_memory_usage_mb = 0;
        size_t total_glyphs = 0;
    };

public:
    FontManager();
    ~FontManager();
    
    // Initialization
    bool initialize();
    
    // Font loading
    uint32_t loadFont(const std::string& font_path, uint32_t font_size, 
                      const std::vector<int>& codepoints = {});
    uint32_t loadFontFromMemory(const void* data, size_t data_size, 
                               uint32_t font_size, const std::string& font_name);
    bool unloadFont(uint32_t font_id);
    
    // Font access
    const FontData* getFont(uint32_t font_id) const;
    Font* getRaylibFont(uint32_t font_id);
    uint32_t getDefaultFontId() const { return m_default_font_id; }
    
    // Font variants
    uint32_t createFontVariant(uint32_t base_font_id, uint32_t new_size);
    
    // Font discovery
    std::vector<uint32_t> getLoadedFontIds() const;
    std::vector<FontInfo> getLoadedFonts() const;
    std::vector<std::string> getAvailableSystemFonts() const;
    void addFontSearchPath(const std::string& path);
    uint32_t findAndLoadFont(const std::string& family_name, uint32_t font_size,
                            const std::string& style = "regular");
    
    // Statistics and maintenance
    Stats getStats() const;
    void optimizeMemory();
    void clear();
    
    // Debug
    std::string getDebugInfo() const;

private:
    // Internal management
    void loadDefaultFont();
    void scanSystemFonts();
    std::string findFontFile(const std::string& family_name, const std::string& style) const;
    void extractFontMetadata(FontData& font_data);
    size_t calculateFontMemoryUsage(const Font& font);
    uint32_t generateFontId();
    void updateFontUsage(uint32_t font_id);

private:
    std::unordered_map<uint32_t, FontData> m_loaded_fonts;
    std::vector<std::string> m_available_fonts;
    std::vector<std::string> m_font_search_paths;
    
    uint32_t m_default_font_id = 0;
    std::atomic<uint32_t> m_next_font_id{1};
    
    mutable std::mutex m_fonts_mutex;
};

} // namespace Kairos