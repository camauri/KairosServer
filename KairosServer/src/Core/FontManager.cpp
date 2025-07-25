// KairosServer/src/Core/FontManager.cpp
#include "FontManager.hpp"
#include "Utils/Logger.hpp"
#include <filesystem>
#include <fstream>
#include <algorithm>

namespace Kairos {

FontManager::FontManager() {
    loadDefaultFont();
    Logger::info("FontManager initialized");
}

FontManager::~FontManager() {
    clear();
}

bool FontManager::initialize() {
    // Initialize font search paths
    addFontSearchPath("/usr/share/fonts/");           // Linux system fonts
    addFontSearchPath("/System/Library/Fonts/");      // macOS system fonts
    addFontSearchPath("C:/Windows/Fonts/");           // Windows system fonts
    addFontSearchPath("./assets/fonts/");             // Local fonts
    addFontSearchPath("./fonts/");                    // Alternative local path
    
    // Scan for available fonts
    scanSystemFonts();
    
    Logger::info("FontManager initialization complete. Found {} system fonts", 
                m_available_fonts.size());
    return true;
}

uint32_t FontManager::loadFont(const std::string& font_path, uint32_t font_size, 
                              const std::vector<int>& codepoints) {
    std::lock_guard<std::mutex> lock(m_fonts_mutex);
    
    // Generate font ID
    uint32_t font_id = generateFontId();
    
    // Check if file exists
    if (!std::filesystem::exists(font_path)) {
        Logger::error("Font file not found: {}", font_path);
        return 0;
    }
    
    try {
        FontData font_data;
        font_data.id = font_id;
        font_data.file_path = font_path;
        font_data.font_size = font_size;
        font_data.load_time = std::chrono::steady_clock::now();
        
        // Load font with Raylib
        if (codepoints.empty()) {
            // Load all available characters
            font_data.raylib_font = LoadFontEx(font_path.c_str(), font_size, nullptr, 0);
        } else {
            // Load specific codepoints
            font_data.raylib_font = LoadFontEx(font_path.c_str(), font_size, 
                                             codepoints.data(), codepoints.size());
            font_data.custom_codepoints = codepoints;
        }
        
        if (font_data.raylib_font.texture.id == 0) {
            Logger::error("Failed to load font: {}", font_path);
            return 0;
        }
        
        // Extract font metadata
        extractFontMetadata(font_data);
        
        // Calculate memory usage
        font_data.memory_usage = calculateFontMemoryUsage(font_data.raylib_font);
        
        // Store font
        m_loaded_fonts[font_id] = std::move(font_data);
        
        Logger::info("Loaded font {} from {} (size={}, memory={}KB)", 
                    font_id, font_path, font_size, font_data.memory_usage / 1024);
        
        return font_id;
        
    } catch (const std::exception& e) {
        Logger::error("Exception loading font {}: {}", font_path, e.what());
        return 0;
    }
}

uint32_t FontManager::loadFontFromMemory(const void* data, size_t data_size, 
                                        uint32_t font_size, const std::string& font_name) {
    std::lock_guard<std::mutex> lock(m_fonts_mutex);
    
    uint32_t font_id = generateFontId();
    
    try {
        FontData font_data;
        font_data.id = font_id;
        font_data.file_path = font_name; // Use name as identifier
        font_data.font_size = font_size;
        font_data.load_time = std::chrono::steady_clock::now();
        font_data.loaded_from_memory = true;
        
        // Load font from memory data
        // Note: Raylib doesn't have direct memory loading, so we'd need to save to temp file
        // For now, this is a placeholder implementation
        Logger::warning("Loading font from memory not fully implemented yet");
        return 0;
        
    } catch (const std::exception& e) {
        Logger::error("Exception loading font from memory: {}", e.what());
        return 0;
    }
}

bool FontManager::unloadFont(uint32_t font_id) {
    if (font_id == m_default_font_id) {
        Logger::warning("Cannot unload default font");
        return false;
    }
    
    std::lock_guard<std::mutex> lock(m_fonts_mutex);
    
    auto it = m_loaded_fonts.find(font_id);
    if (it == m_loaded_fonts.end()) {
        Logger::warning("Font {} not found for unloading", font_id);
        return false;
    }
    
    // Unload Raylib font
    if (it->second.raylib_font.texture.id != 0) {
        UnloadFont(it->second.raylib_font);
    }
    
    Logger::debug("Unloaded font {} ({})", font_id, it->second.file_path);
    m_loaded_fonts.erase(it);
    
    return true;
}

const FontManager::FontData* FontManager::getFont(uint32_t font_id) const {
    std::lock_guard<std::mutex> lock(m_fonts_mutex);
    
    auto it = m_loaded_fonts.find(font_id);
    if (it != m_loaded_fonts.end()) {
        return &it->second;
    }
    
    // Return default font as fallback
    auto default_it = m_loaded_fonts.find(m_default_font_id);
    if (default_it != m_loaded_fonts.end()) {
        return &default_it->second;
    }
    
    return nullptr;
}

Font* FontManager::getRaylibFont(uint32_t font_id) {
    std::lock_guard<std::mutex> lock(m_fonts_mutex);
    
    auto it = m_loaded_fonts.find(font_id);
    if (it != m_loaded_fonts.end()) {
        return &it->second.raylib_font;
    }
    
    // Return default font as fallback
    auto default_it = m_loaded_fonts.find(m_default_font_id);
    if (default_it != m_loaded_fonts.end()) {
        return &default_it->second.raylib_font;
    }
    
    return nullptr;
}

uint32_t FontManager::createFontVariant(uint32_t base_font_id, uint32_t new_size) {
    const FontData* base_font = getFont(base_font_id);
    if (!base_font) {
        Logger::error("Base font {} not found for variant creation", base_font_id);
        return 0;
    }
    
    // Load new variant with different size
    if (base_font->custom_codepoints.empty()) {
        return loadFont(base_font->file_path, new_size);
    } else {
        return loadFont(base_font->file_path, new_size, base_font->custom_codepoints);
    }
}

std::vector<uint32_t> FontManager::getLoadedFontIds() const {
    std::lock_guard<std::mutex> lock(m_fonts_mutex);
    
    std::vector<uint32_t> font_ids;
    for (const auto& [font_id, font_data] : m_loaded_fonts) {
        font_ids.push_back(font_id);
    }
    
    return font_ids;
}

std::vector<FontManager::FontInfo> FontManager::getLoadedFonts() const {
    std::lock_guard<std::mutex> lock(m_fonts_mutex);
    
    std::vector<FontInfo> fonts;
    for (const auto& [font_id, font_data] : m_loaded_fonts) {
        FontInfo info;
        info.id = font_id;
        info.family_name = font_data.metadata.family_name;
        info.style_name = font_data.metadata.style_name;
        info.file_path = font_data.file_path;
        info.font_size = font_data.font_size;
        info.memory_usage = font_data.memory_usage;
        info.glyph_count = font_data.raylib_font.glyphCount;
        info.is_default = (font_id == m_default_font_id);
        
        fonts.push_back(info);
    }
    
    return fonts;
}

std::vector<std::string> FontManager::getAvailableSystemFonts() const {
    std::lock_guard<std::mutex> lock(m_fonts_mutex);
    return m_available_fonts;
}

void FontManager::addFontSearchPath(const std::string& path) {
    std::lock_guard<std::mutex> lock(m_fonts_mutex);
    
    if (std::filesystem::exists(path)) {
        m_font_search_paths.push_back(path);
        Logger::debug("Added font search path: {}", path);
    } else {
        Logger::debug("Font search path does not exist: {}", path);
    }
}

uint32_t FontManager::findAndLoadFont(const std::string& family_name, uint32_t font_size,
                                     const std::string& style) {
    // Search in available fonts
    std::string font_path = findFontFile(family_name, style);
    
    if (font_path.empty()) {
        Logger::warning("Font not found: {} {}", family_name, style);
        return m_default_font_id;
    }
    
    return loadFont(font_path, font_size);
}

FontManager::Stats FontManager::getStats() const {
    std::lock_guard<std::mutex> lock(m_fonts_mutex);
    
    Stats stats;
    stats.loaded_fonts = m_loaded_fonts.size();
    stats.available_system_fonts = m_available_fonts.size();
    stats.default_font_id = m_default_font_id;
    
    for (const auto& [font_id, font_data] : m_loaded_fonts) {
        stats.total_memory_usage += font_data.memory_usage;
        stats.total_glyphs += font_data.raylib_font.glyphCount;
    }
    
    stats.total_memory_usage_mb = stats.total_memory_usage / (1024 * 1024);
    
    return stats;
}

void FontManager::optimizeMemory() {
    std::lock_guard<std::mutex> lock(m_fonts_mutex);
    
    auto now = std::chrono::steady_clock::now();
    std::vector<uint32_t> fonts_to_unload;
    
    for (const auto& [font_id, font_data] : m_loaded_fonts) {
        // Skip default font
        if (font_id == m_default_font_id) {
            continue;
        }
        
        // Unload fonts that haven't been used for a while
        auto idle_time = std::chrono::duration_cast<std::chrono::minutes>(
            now - font_data.last_used);
        
        if (idle_time.count() > 10) { // 10 minutes idle
            fonts_to_unload.push_back(font_id);
        }
    }
    
    // Unload unused fonts
    for (uint32_t font_id : fonts_to_unload) {
        auto it = m_loaded_fonts.find(font_id);
        if (it != m_loaded_fonts.end()) {
            if (it->second.raylib_font.texture.id != 0) {
                UnloadFont(it->second.raylib_font);
            }
            Logger::debug("Unloaded unused font {} ({})", font_id, it->second.file_path);
            m_loaded_fonts.erase(it);
        }
    }
    
    if (!fonts_to_unload.empty()) {
        Logger::info("Memory optimization: unloaded {} unused fonts", fonts_to_unload.size());
    }
}

void FontManager::clear() {
    std::lock_guard<std::mutex> lock(m_fonts_mutex);
    
    // Unload all fonts except default
    for (auto& [font_id, font_data] : m_loaded_fonts) {
        if (font_id != m_default_font_id && font_data.raylib_font.texture.id != 0) {
            UnloadFont(font_data.raylib_font);
        }
    }
    
    m_loaded_fonts.clear();
    
    // Reload default font
    loadDefaultFont();
    
    Logger::debug("FontManager cleared and reset");
}

// Private methods

void FontManager::loadDefaultFont() {
    std::lock_guard<std::mutex> lock(m_fonts_mutex);
    
    m_default_font_id = generateFontId();
    
    FontData default_font;
    default_font.id = m_default_font_id;
    default_font.file_path = "default";
    default_font.font_size = 16;
    default_font.load_time = std::chrono::steady_clock::now();
    default_font.last_used = default_font.load_time;
    default_font.raylib_font = GetFontDefault();
    
    // Set metadata for default font
    default_font.metadata.family_name = "Default";
    default_font.metadata.style_name = "Regular";
    default_font.metadata.is_monospace = false;
    default_font.metadata.has_kerning = false;
    
    default_font.memory_usage = calculateFontMemoryUsage(default_font.raylib_font);
    
    m_loaded_fonts[m_default_font_id] = std::move(default_font);
    
    Logger::debug("Default font loaded with ID {}", m_default_font_id);
}

void FontManager::scanSystemFonts() {
    m_available_fonts.clear();
    
    for (const std::string& search_path : m_font_search_paths) {
        if (!std::filesystem::exists(search_path)) {
            continue;
        }
        
        try {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(search_path)) {
                if (entry.is_regular_file()) {
                    std::string extension = entry.path().extension().string();
                    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
                    
                    if (extension == ".ttf" || extension == ".otf" || 
                        extension == ".ttc" || extension == ".otc") {
                        m_available_fonts.push_back(entry.path().string());
                    }
                }
            }
        } catch (const std::filesystem::filesystem_error& e) {
            Logger::warning("Error scanning font directory {}: {}", search_path, e.what());
        }
    }
    
    Logger::debug("Scanned system fonts: found {} font files", m_available_fonts.size());
}

std::string FontManager::findFontFile(const std::string& family_name, const std::string& style) const {
    std::string target_family = family_name;
    std::transform(target_family.begin(), target_family.end(), target_family.begin(), ::tolower);
    
    std::string target_style = style;
    std::transform(target_style.begin(), target_style.end(), target_style.begin(), ::tolower);
    
    // Simple heuristic font matching
    for (const std::string& font_path : m_available_fonts) {
        std::string filename = std::filesystem::path(font_path).filename().string();
        std::transform(filename.begin(), filename.end(), filename.begin(), ::tolower);
        
        // Check if family name is in filename
        if (filename.find(target_family) != std::string::npos) {
            // If style is specified, check for it too
            if (!target_style.empty() && target_style != "regular") {
                if (filename.find(target_style) != std::string::npos ||
                    filename.find("bold") != std::string::npos ||
                    filename.find("italic") != std::string::npos) {
                    return font_path;
                }
            } else {
                // For regular style, prefer files without style indicators
                if (filename.find("bold") == std::string::npos &&
                    filename.find("italic") == std::string::npos &&
                    filename.find("light") == std::string::npos) {
                    return font_path;
                }
            }
        }
    }
    
    return ""; // Not found
}

void FontManager::extractFontMetadata(FontData& font_data) {
    // This is a simplified metadata extraction
    // In a real implementation, you might use libraries like FreeType for detailed metadata
    
    std::filesystem::path path(font_data.file_path);
    std::string filename = path.stem().string();
    
    // Simple heuristics for font metadata
    font_data.metadata.family_name = filename;
    font_data.metadata.style_name = "Regular";
    
    // Check for common style indicators in filename
    std::string lower_filename = filename;
    std::transform(lower_filename.begin(), lower_filename.end(), lower_filename.begin(), ::tolower);
    
    if (lower_filename.find("bold") != std::string::npos) {
        font_data.metadata.style_name = "Bold";
    } else if (lower_filename.find("italic") != std::string::npos) {
        font_data.metadata.style_name = "Italic";
    } else if (lower_filename.find("light") != std::string::npos) {
        font_data.metadata.style_name = "Light";
    }
    
    // Check for monospace indicators
    font_data.metadata.is_monospace = (lower_filename.find("mono") != std::string::npos ||
                                      lower_filename.find("courier") != std::string::npos ||
                                      lower_filename.find("consol") != std::string::npos);
    
    // Assume modern fonts have kerning
    font_data.metadata.has_kerning = true;
}

size_t FontManager::calculateFontMemoryUsage(const Font& font) {
    if (font.texture.id == 0) {
        return 0;
    }
    
    // Calculate texture memory usage
    size_t texture_memory = font.texture.width * font.texture.height;
    
    // Assume RGBA format for font textures
    texture_memory *= 4;
    
    // Add glyph data memory
    size_t glyph_memory = font.glyphCount * sizeof(GlyphInfo);
    
    return texture_memory + glyph_memory;
}

uint32_t FontManager::generateFontId() {
    return m_next_font_id++;
}

void FontManager::updateFontUsage(uint32_t font_id) {
    std::lock_guard<std::mutex> lock(m_fonts_mutex);
    
    auto it = m_loaded_fonts.find(font_id);
    if (it != m_loaded_fonts.end()) {
        it->second.last_used = std::chrono::steady_clock::now();
        it->second.usage_count++;
    }
}

std::string FontManager::getDebugInfo() const {
    std::lock_guard<std::mutex> lock(m_fonts_mutex);
    
    std::stringstream ss;
    ss << "FontManager Debug Info:\n";
    ss << "Loaded fonts: " << m_loaded_fonts.size() << "\n";
    ss << "Available system fonts: " << m_available_fonts.size() << "\n";
    ss << "Default font ID: " << m_default_font_id << "\n";
    ss << "Search paths: " << m_font_search_paths.size() << "\n\n";
    
    for (const auto& [font_id, font_data] : m_loaded_fonts) {
        ss << "Font " << font_id << ":\n";
        ss << "  Family: " << font_data.metadata.family_name << "\n";
        ss << "  Style: " << font_data.metadata.style_name << "\n";
        ss << "  Size: " << font_data.font_size << "\n";
        ss << "  File: " << font_data.file_path << "\n";
        ss << "  Memory: " << (font_data.memory_usage / 1024) << " KB\n";
        ss << "  Glyphs: " << font_data.raylib_font.glyphCount << "\n";
        ss << "  Usage count: " << font_data.usage_count << "\n";
        ss << "  Monospace: " << (font_data.metadata.is_monospace ? "Yes" : "No") << "\n";
        
        auto age = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - font_data.load_time);
        ss << "  Age: " << age.count() << "s\n";
        
        auto last_used = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - font_data.last_used);
        ss << "  Last used: " << last_used.count() << "s ago\n\n";
    }
    
    return ss.str();
}

} // namespace Kairos