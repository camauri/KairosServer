// KairosServer/src/Core/RaylibRenderer.cpp
#include "RaylibRenderer.hpp"
#include "Utils/Logger.hpp"
#include <chrono>
#include <algorithm>
#include <cstring>

namespace Kairos {

RaylibRenderer::RaylibRenderer(const Config& config) 
    : m_config(config) {
    
    // Initialize camera
    m_camera2d = { 0 };
    m_camera2d.target = { 0.0f, 0.0f };
    m_camera2d.offset = { 0.0f, 0.0f };
    m_camera2d.rotation = 0.0f;
    m_camera2d.zoom = 1.0f;
    
    // Reserve batch groups
    m_batch_groups.reserve(256);  // Reasonable default
    
    Logger::info("RaylibRenderer created with {}x{} resolution", 
                m_config.window_width, m_config.window_height);
}

RaylibRenderer::~RaylibRenderer() {
    if (m_initialized) {
        shutdown();
    }
}

bool RaylibRenderer::initialize() {
    if (m_initialized) {
        Logger::warning("RaylibRenderer already initialized");
        return true;
    }
    
    Logger::info("Initializing RaylibRenderer...");
    
    try {
        // Set Raylib configuration flags
        unsigned int flags = 0;
        
        if (m_config.enable_antialiasing) {
            flags |= FLAG_MSAA_4X_HINT;
        }
        
        if (m_config.enable_vsync) {
            flags |= FLAG_VSYNC_HINT;
        }
        
        if (m_config.fullscreen) {
            flags |= FLAG_FULLSCREEN_MODE;
        }
        
        if (m_config.hidden) {
            flags |= FLAG_WINDOW_HIDDEN;
        }
        
        SetConfigFlags(flags);
        
        // Initialize window
        InitWindow(m_config.window_width, m_config.window_height, 
                  m_config.window_title.c_str());
        
        if (!IsWindowReady()) {
            Logger::error("Failed to create Raylib window");
            return false;
        }
        
        // Set target FPS (0 = uncapped for manual control)
        SetTargetFPS(m_config.target_fps);
        
        // Initialize default resources
        initializeDefaultResources();
        
        // Initialize layer caches if enabled
        if (m_config.layer_caching) {
            // Pre-create layer 0 (always exists)
            getOrCreateLayerCache(0);
        }
        
        m_initialized = true;
        
        Logger::info("RaylibRenderer initialized successfully");
        Logger::info("Raylib version: {}", GetRaylibVersion());
        Logger::info("OpenGL version: {}.{}", 
                    rlGetVersion().major, rlGetVersion().minor);
        
        return true;
        
    } catch (const std::exception& e) {
        Logger::error("Exception during RaylibRenderer initialization: {}", e.what());
        return false;
    }
}

void RaylibRenderer::shutdown() {
    if (!m_initialized) {
        return;
    }
    
    Logger::info("Shutting down RaylibRenderer...");
    
    // Clean up resources
    cleanupResources();
    
    // Close Raylib window
    if (IsWindowReady()) {
        CloseWindow();
    }
    
    m_initialized = false;
    Logger::info("RaylibRenderer shutdown complete");
}

void RaylibRenderer::beginFrame() {
    if (!m_initialized) {
        return;
    }
    
    m_frame_start_time = std::chrono::steady_clock::now();
    
    // Begin Raylib drawing
    BeginDrawing();
    
    // Clear background
    ClearBackground(BLACK);
    
    // Apply camera if needed
    if (m_using_camera2d) {
        BeginMode2D(m_camera2d);
    }
    
    // Reset per-frame stats
    m_stats.queued_commands.store(0);
    m_stats.batched_draws.store(0);
}

void RaylibRenderer::endFrame() {
    if (!m_initialized) {
        return;
    }
    
    // Flush any remaining batches
    flushBatches();
    
    // Render all layers
    renderLayers();
    
    // End camera mode if active
    if (m_using_camera2d) {
        EndMode2D();
    }
    
    // End Raylib drawing
    EndDrawing();
    
    // Update statistics
    updateStats();
    
    // Check if window should close
    m_window_should_close = WindowShouldClose();
    
    m_stats.frames_rendered++;
}

bool RaylibRenderer::shouldClose() const {
    return m_window_should_close;
}

void RaylibRenderer::processCommand(const RenderCommand& command) {
    if (!m_initialized) {
        Logger::warning("Attempting to process command on uninitialized renderer");
        return;
    }
    
    m_stats.commands_processed++;
    m_stats.queued_commands.fetch_add(1);
    
    switch (command.type) {
        case RenderCommand::Type::DRAW_POINT:
            drawPoint(command.point.position, command.point.color, command.layer_id);
            break;
            
        case RenderCommand::Type::DRAW_LINE:
            drawLine(command.line.start, command.line.end, 
                    command.line.color, command.line.thickness, command.layer_id);
            break;
            
        case RenderCommand::Type::DRAW_RECTANGLE:
            drawRectangle(command.rectangle.position, 
                         command.rectangle.width, command.rectangle.height,
                         command.rectangle.color, command.rectangle.filled, 
                         command.layer_id);
            break;
            
        case RenderCommand::Type::DRAW_CIRCLE:
            drawCircle(command.circle.center, command.circle.radius,
                      command.circle.color, command.circle.filled, command.layer_id);
            break;
            
        case RenderCommand::Type::DRAW_TEXT:
            drawText(command.text.text, command.text.position,
                    command.text.font_id, command.text.font_size,
                    command.text.color, command.layer_id);
            break;
            
        case RenderCommand::Type::DRAW_TEXTURED_QUADS:
            drawTexturedQuads(command.textured_quads.vertices,
                             command.textured_quads.texture_id, command.layer_id);
            break;
            
        case RenderCommand::Type::CLEAR_LAYER:
            clearLayer(command.layer_id);
            break;
            
        default:
            Logger::warning("Unknown render command type: {}", 
                           static_cast<int>(command.type));
            break;
    }
}

void RaylibRenderer::processCommands(const std::vector<RenderCommand>& commands) {
    for (const auto& command : commands) {
        processCommand(command);
    }
}

void RaylibRenderer::drawPoint(const Point& position, const Color& color, uint8_t layer_id) {
    // Create a small rectangle for the point (Raylib doesn't have direct pixel drawing)
    Vector2 pos = pointToVector2(position);
    ::Color raylib_color = kairosColorToRaylib(color);
    
    // For now, draw directly - in a real implementation, this should be batched
    DrawPixel(static_cast<int>(pos.x), static_cast<int>(pos.y), raylib_color);
    
    m_stats.vertices_rendered++;
}

void RaylibRenderer::drawLine(const Point& start, const Point& end, 
                             const Color& color, float thickness, uint8_t layer_id) {
    Vector2 start_pos = pointToVector2(start);
    Vector2 end_pos = pointToVector2(end);
    ::Color raylib_color = kairosColorToRaylib(color);
    
    if (thickness <= 1.0f) {
        DrawLineV(start_pos, end_pos, raylib_color);
    } else {
        DrawLineEx(start_pos, end_pos, thickness, raylib_color);
    }
    
    m_stats.vertices_rendered += 2;
    m_stats.draw_calls_issued++;
}

void RaylibRenderer::drawRectangle(const Point& position, float width, float height,
                                  const Color& color, bool filled, uint8_t layer_id) {
    Vector2 pos = pointToVector2(position);
    ::Color raylib_color = kairosColorToRaylib(color);
    
    if (filled) {
        DrawRectangleV(pos, {width, height}, raylib_color);
    } else {
        DrawRectangleLinesEx({pos.x, pos.y, width, height}, 1.0f, raylib_color);
    }
    
    m_stats.vertices_rendered += 4;
    m_stats.draw_calls_issued++;
}

void RaylibRenderer::drawCircle(const Point& center, float radius, 
                               const Color& color, bool filled, uint8_t layer_id) {
    Vector2 center_pos = pointToVector2(center);
    ::Color raylib_color = kairosColorToRaylib(color);
    
    if (filled) {
        DrawCircleV(center_pos, radius, raylib_color);
    } else {
        DrawCircleLinesV(center_pos, radius, raylib_color);
    }
    
    // Estimate vertices for circle (depends on radius)
    int segments = static_cast<int>(radius * 0.5f) + 12;
    m_stats.vertices_rendered += segments;
    m_stats.draw_calls_issued++;
}

void RaylibRenderer::drawText(const std::string& text, const Point& position,
                             uint32_t font_id, float font_size, 
                             const Color& color, uint8_t layer_id) {
    Vector2 pos = pointToVector2(position);
    ::Color raylib_color = kairosColorToRaylib(color);
    
    Font* font = getFont(font_id);
    if (font && font->texture.id != 0) {
        DrawTextEx(*font, text.c_str(), pos, font_size, 1.0f, raylib_color);
    } else {
        // Fallback to default font
        DrawText(text.c_str(), static_cast<int>(pos.x), static_cast<int>(pos.y), 
                static_cast<int>(font_size), raylib_color);
    }
    
    // Estimate vertices for text
    m_stats.vertices_rendered += text.length() * 6; // 2 triangles per character
    m_stats.draw_calls_issued++;
}

void RaylibRenderer::drawTexturedQuads(const std::vector<TexturedVertex>& vertices,
                                      uint32_t texture_id, uint8_t layer_id) {
    if (vertices.empty()) {
        return;
    }
    
    Texture2D* texture = getTexture(texture_id);
    if (!texture || texture->id == 0) {
        Logger::warning("Invalid texture ID: {}", texture_id);
        return;
    }
    
    // Add to batch for efficient rendering
    addToBatch(texture_id, vertices, {255, 255, 255, 255}, layer_id);
    
    m_stats.vertices_rendered += vertices.size();
}

void RaylibRenderer::flushBatches() {
    std::lock_guard<std::mutex> lock(m_batch_mutex);
    
    for (auto& batch : m_batch_groups) {
        if (!batch.isEmpty() && batch.needs_flush) {
            flushBatch(batch);
        }
    }
    
    // Clear batches
    m_batch_groups.clear();
}

void RaylibRenderer::renderLayers() {
    if (m_config.layer_caching) {
        compositeLayerCaches();
    } else {
        // Direct rendering without caching
        for (uint8_t layer_id = 0; layer_id < m_config.max_layers; ++layer_id) {
            renderLayer(layer_id);
        }
    }
}

void RaylibRenderer::renderLayer(uint8_t layer_id) {
    // This is a simplified version - in practice, layers would maintain
    // their own command lists or render to textures
    
    m_stats.active_layers++;
}

uint32_t RaylibRenderer::uploadTexture(uint32_t texture_id, uint32_t width, uint32_t height,
                                      uint32_t format, const void* pixel_data, uint32_t data_size) {
    std::lock_guard<std::mutex> lock(m_resource_mutex);
    
    if (texture_id == 0) {
        texture_id = generateResourceId();
    }
    
    try {
        // Create Raylib texture
        Image image = {0};
        image.width = width;
        image.height = height;
        image.mipmaps = 1;
        
        // Convert format
        switch (format) {
            case Constants::PIXEL_FORMAT_RGBA8:
                image.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
                break;
            case Constants::PIXEL_FORMAT_RGB8:
                image.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8;
                break;
            case Constants::PIXEL_FORMAT_ALPHA8:
                image.format = PIXELFORMAT_UNCOMPRESSED_GRAYSCALE;
                break;
            default:
                Logger::error("Unsupported pixel format: {}", format);
                return 0;
        }
        
        // Allocate and copy pixel data
        size_t expected_size = width * height * GetPixelDataSize(width, height, image.format);
        if (data_size != expected_size) {
            Logger::warning("Pixel data size mismatch: expected {}, got {}", 
                           expected_size, data_size);
        }
        
        image.data = MemAlloc(data_size);
        std::memcpy(image.data, pixel_data, data_size);
        
        // Create texture from image
        Texture2D texture = LoadTextureFromImage(image);
        
        // Clean up image data
        UnloadImage(image);
        
        if (texture.id == 0) {
            Logger::error("Failed to create texture from image data");
            return 0;
        }
        
        // Store texture
        m_textures[texture_id] = texture;
        m_stats.textures_uploaded++;
        
        Logger::debug("Uploaded texture {} ({}x{}, format={})", 
                     texture_id, width, height, format);
        
        return texture_id;
        
    } catch (const std::exception& e) {
        Logger::error("Exception during texture upload: {}", e.what());
        return 0;
    }
}

Texture2D* RaylibRenderer::getTexture(uint32_t texture_id) {
    std::lock_guard<std::mutex> lock(m_resource_mutex);
    auto it = m_textures.find(texture_id);
    return (it != m_textures.end()) ? &it->second : nullptr;
}

uint32_t RaylibRenderer::loadFont(const std::string& font_path, uint32_t font_size) {
    std::lock_guard<std::mutex> lock(m_resource_mutex);
    
    uint32_t font_id = generateResourceId();
    
    try {
        Font font = LoadFontEx(font_path.c_str(), font_size, nullptr, 0);
        
        if (font.texture.id == 0) {
            Logger::error("Failed to load font: {}", font_path);
            return 0;
        }
        
        m_fonts[font_id] = font;
        
        Logger::info("Loaded font {} from {} (size={})", font_id, font_path, font_size);
        return font_id;
        
    } catch (const std::exception& e) {
        Logger::error("Exception during font loading: {}", e.what());
        return 0;
    }
}

Font* RaylibRenderer::getFont(uint32_t font_id) {
    std::lock_guard<std::mutex> lock(m_resource_mutex);
    auto it = m_fonts.find(font_id);
    return (it != m_fonts.end()) ? &it->second : nullptr;
}

void RaylibRenderer::clearLayer(uint8_t layer_id) {
    if (m_config.layer_caching) {
        auto* cache = getOrCreateLayerCache(layer_id);
        if (cache) {
            cache->is_dirty = true;
            cache->commands.clear();
        }
    }
}

void RaylibRenderer::clearAllLayers() {
    if (m_config.layer_caching) {
        for (auto& [layer_id, cache] : m_layer_caches) {
            cache.is_dirty = true;
            cache.commands.clear();
        }
    }
}

// Private methods implementation

void RaylibRenderer::initializeDefaultResources() {
    // Create default white texture (1x1 white pixel)
    uint32_t white_pixel = 0xFFFFFFFF;
    m_white_texture_id = uploadTexture(0, 1, 1, Constants::PIXEL_FORMAT_RGBA8, 
                                      &white_pixel, sizeof(white_pixel));
    
    // Load default font (Raylib's default)
    m_default_font_id = generateResourceId();
    m_fonts[m_default_font_id] = GetFontDefault();
    
    Logger::debug("Default resources initialized (white_texture={}, default_font={})",
                 m_white_texture_id, m_default_font_id);
}

void RaylibRenderer::cleanupResources() {
    std::lock_guard<std::mutex> lock(m_resource_mutex);
    
    // Unload textures
    for (auto& [id, texture] : m_textures) {
        if (texture.id != 0) {
            UnloadTexture(texture);
        }
    }
    m_textures.clear();
    
    // Unload fonts (except default)
    for (auto& [id, font] : m_fonts) {
        if (id != m_default_font_id && font.texture.id != 0) {
            UnloadFont(font);
        }
    }
    m_fonts.clear();
    
    // Clean up layer caches
    for (auto& [id, cache] : m_layer_caches) {
        if (cache.render_texture.id != 0) {
            UnloadRenderTexture(cache.render_texture);
        }
    }
    m_layer_caches.clear();
    
    Logger::debug("Resources cleaned up");
}

uint32_t RaylibRenderer::generateResourceId() {
    return m_next_resource_id.fetch_add(1);
}

void RaylibRenderer::addToBatch(uint32_t texture_id, const std::vector<TexturedVertex>& vertices,
                               const Color& tint, uint8_t layer_id) {
    std::lock_guard<std::mutex> lock(m_batch_mutex);
    
    // Find or create batch group
    BatchGroup* target_batch = nullptr;
    
    for (auto& batch : m_batch_groups) {
        if (batch.texture_id == texture_id && 
            batch.layer_id == layer_id &&
            batch.tint_color.rgba == tint.rgba) {
            target_batch = &batch;
            break;
        }
    }
    
    if (!target_batch) {
        m_batch_groups.emplace_back();
        target_batch = &m_batch_groups.back();
        target_batch->texture_id = texture_id;
        target_batch->tint_color = tint;
        target_batch->layer_id = layer_id;
    }
    
    // Add vertices to batch
    target_batch->vertices.insert(target_batch->vertices.end(), 
                                 vertices.begin(), vertices.end());
    target_batch->needs_flush = true;
    
    // Check if batch is full
    if (target_batch->vertices.size() >= m_config.max_batch_size) {
        flushBatch(*target_batch);
    }
}

void RaylibRenderer::flushBatch(BatchGroup& batch) {
    if (batch.isEmpty()) {
        return;
    }
    
    Texture2D* texture = getTexture(batch.texture_id);
    if (!texture) {
        Logger::warning("Cannot flush batch: invalid texture {}", batch.texture_id);
        batch.clear();
        return;
    }
    
    // Convert vertices to Raylib format and draw
    // This is a simplified version - real implementation would use vertex buffers
    ::Color tint = kairosColorToRaylib(batch.tint_color);
    
    // Draw in groups of 4 vertices (quads)
    for (size_t i = 0; i + 3 < batch.vertices.size(); i += 4) {
        const auto& v0 = batch.vertices[i];
        const auto& v1 = batch.vertices[i + 1];
        const auto& v2 = batch.vertices[i + 2];
        const auto& v3 = batch.vertices[i + 3];
        
        // Draw textured quad
        Rectangle source = {
            v0.u * texture->width, v0.v * texture->height,
            (v2.u - v0.u) * texture->width, (v2.v - v0.v) * texture->height
        };
        
        Rectangle dest = {
            v0.x, v0.y,
            v2.x - v0.x, v2.y - v0.y
        };
        
        DrawTexturePro(*texture, source, dest, {0, 0}, 0.0f, tint);
    }
    
    m_stats.draw_calls_issued++;
    m_stats.batched_draws.fetch_add(1);
    
    batch.clear();
}

RaylibRenderer::LayerCache* RaylibRenderer::getOrCreateLayerCache(uint8_t layer_id) {
    auto it = m_layer_caches.find(layer_id);
    if (it != m_layer_caches.end()) {
        return &it->second;
    }
    
    // Create new layer cache
    LayerCache cache;
    cache.render_texture = LoadRenderTexture(m_config.window_width, m_config.window_height);
    cache.is_dirty = true;
    cache.is_visible = true;
    
    if (cache.render_texture.id == 0) {
        Logger::error("Failed to create render texture for layer {}", layer_id);
        return nullptr;
    }
    
    m_layer_caches[layer_id] = cache;
    Logger::debug("Created layer cache for layer {}", layer_id);
    
    return &m_layer_caches[layer_id];
}

void RaylibRenderer::renderToLayerCache(LayerCache& cache) {
    if (!cache.is_dirty) {
        return;
    }
    
    // Begin rendering to layer texture
    BeginTextureMode(cache.render_texture);
    ClearBackground(BLANK);  // Transparent background
    
    // Render all commands for this layer
    for (const auto& command : cache.commands) {
        processCommand(command);
    }
    
    EndTextureMode();
    
    cache.is_dirty = false;
    cache.last_update_frame = m_stats.frames_rendered;
    m_stats.cached_layers++;
}

void RaylibRenderer::compositeLayerCaches() {
    // Render layers in order from 0 to max
    for (uint8_t layer_id = 0; layer_id < m_config.max_layers; ++layer_id) {
        auto it = m_layer_caches.find(layer_id);
        if (it != m_layer_caches.end() && it->second.is_visible) {
            LayerCache& cache = it->second;
            
            // Update cache if dirty
            if (cache.is_dirty) {
                renderToLayerCache(cache);
            }
            
            // Composite layer to screen
            Rectangle source = {0, 0, 
                              static_cast<float>(cache.render_texture.texture.width),
                              -static_cast<float>(cache.render_texture.texture.height)};
            Rectangle dest = {0, 0, 
                            static_cast<float>(m_config.window_width),
                            static_cast<float>(m_config.window_height)};
            
            // Set blend mode for layer
            BeginBlendMode(cache.blend_mode);
            DrawTexturePro(cache.render_texture.texture, source, dest, {0, 0}, 0.0f, WHITE);
            EndBlendMode();
            
            m_stats.draw_calls_issued++;
        }
    }
}

void RaylibRenderer::updateStats() {
    auto now = std::chrono::steady_clock::now();
    auto frame_duration = std::chrono::duration_cast<std::chrono::microseconds>(
        now - m_frame_start_time);
    
    m_stats.avg_frame_time_ms = frame_duration.count() / 1000.0f;
    
    // Update FPS every second
    if (now - m_last_fps_update >= std::chrono::seconds(1)) {
        m_stats.current_fps = static_cast<float>(m_frame_count_for_fps);
        m_frame_count_for_fps = 0;
        m_last_fps_update = now;
    } else {
        m_frame_count_for_fps++;
    }
    
    // Estimate memory usage
    size_t texture_memory = 0;
    for (const auto& [id, texture] : m_textures) {
        texture_memory += texture.width * texture.height * 4; // Assume RGBA
    }
    
    size_t layer_memory = 0;
    for (const auto& [id, cache] : m_layer_caches) {
        layer_memory += m_config.window_width * m_config.window_height * 4; // RGBA
    }
    
    m_stats.memory_usage_mb = static_cast<uint32_t>((texture_memory + layer_memory) / (1024 * 1024));
}

void RaylibRenderer::setLayerVisibility(uint8_t layer_id, bool visible) {
    if (m_config.layer_caching) {
        auto* cache = getOrCreateLayerCache(layer_id);
        if (cache) {
            cache->is_visible = visible;
        }
    }
}

void RaylibRenderer::setLayerBlendMode(uint8_t layer_id, int blend_mode) {
    if (m_config.layer_caching) {
        auto* cache = getOrCreateLayerCache(layer_id);
        if (cache) {
            cache->blend_mode = blend_mode;
        }
    }
}

bool RaylibRenderer::deleteTexture(uint32_t texture_id) {
    std::lock_guard<std::mutex> lock(m_resource_mutex);
    
    auto it = m_textures.find(texture_id);
    if (it != m_textures.end()) {
        if (it->second.id != 0) {
            UnloadTexture(it->second);
        }
        m_textures.erase(it);
        Logger::debug("Deleted texture {}", texture_id);
        return true;
    }
    
    return false;
}

bool RaylibRenderer::deleteFont(uint32_t font_id) {
    std::lock_guard<std::mutex> lock(m_resource_mutex);
    
    if (font_id == m_default_font_id) {
        Logger::warning("Cannot delete default font {}", font_id);
        return false;
    }
    
    auto it = m_fonts.find(font_id);
    if (it != m_fonts.end()) {
        if (it->second.texture.id != 0) {
            UnloadFont(it->second);
        }
        m_fonts.erase(it);
        Logger::debug("Deleted font {}", font_id);
        return true;
    }
    
    return false;
}

void RaylibRenderer::setViewport(int x, int y, int width, int height) {
    // Raylib doesn't have explicit viewport setting in the same way as OpenGL
    // This could be implemented using render textures or scissors
    Logger::debug("Viewport set to ({}, {}, {}, {})", x, y, width, height);
}

void RaylibRenderer::setCamera2D(const Vector2& target, const Vector2& offset, 
                                 float rotation, float zoom) {
    m_camera2d.target = target;
    m_camera2d.offset = offset;
    m_camera2d.rotation = rotation;
    m_camera2d.zoom = zoom;
    m_using_camera2d = true;
    
    Logger::debug("Camera2D set: target=({:.1f}, {:.1f}), zoom={:.2f}", 
                 target.x, target.y, zoom);
}

void RaylibRenderer::resetCamera2D() {
    m_camera2d.target = {0.0f, 0.0f};
    m_camera2d.offset = {0.0f, 0.0f};
    m_camera2d.rotation = 0.0f;
    m_camera2d.zoom = 1.0f;
    m_using_camera2d = false;
}

void RaylibRenderer::resetStats() {
    std::lock_guard<std::mutex> lock(m_stats_mutex);
    m_stats = Stats{};
    Logger::debug("Renderer statistics reset");
}

void RaylibRenderer::setConfig(const Config& config) {
    m_config = config;
    
    if (m_initialized) {
        // Apply config changes that can be applied at runtime
        SetTargetFPS(m_config.target_fps);
        
        Logger::info("Renderer configuration updated");
    }
}

void RaylibRenderer::handleWindowResize(int width, int height) {
    m_config.window_width = width;
    m_config.window_height = height;
    
    // Recreate layer caches with new size
    if (m_config.layer_caching) {
        for (auto& [layer_id, cache] : m_layer_caches) {
            if (cache.render_texture.id != 0) {
                UnloadRenderTexture(cache.render_texture);
            }
            cache.render_texture = LoadRenderTexture(width, height);
            cache.is_dirty = true;
        }
    }
    
    Logger::info("Window resized to {}x{}", width, height);
}

// Utility functions implementation

Vector2 pointToVector2(const Point& point) {
    return {point.x, point.y};
}

Point vector2ToPoint(const Vector2& vector) {
    return {vector.x, vector.y};
}

::Color kairosColorToRaylib(const Kairos::Color& color) {
    return {color.r, color.g, color.b, color.a};
}

Kairos::Color raylibColorToKairos(const ::Color& color) {
    return Kairos::Color{color.r, color.g, color.b, color.a};
}

} // namespace Kairos