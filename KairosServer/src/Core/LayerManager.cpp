// KairosServer/src/Core/LayerManager.cpp
#include "LayerManager.hpp"
#include "Utils/Logger.hpp"
#include <algorithm>

namespace Kairos {

LayerManager::LayerManager(uint32_t max_layers) 
    : m_max_layers(max_layers) {
    
    // Create default layer 0
    createLayer(0);
    
    Logger::info("LayerManager initialized with max {} layers", max_layers);
}

LayerManager::~LayerManager() {
    clear();
}

bool LayerManager::createLayer(uint8_t layer_id) {
    std::lock_guard<std::mutex> lock(m_layers_mutex);
    
    if (layer_id >= m_max_layers) {
        Logger::warning("Layer ID {} exceeds maximum {}", layer_id, m_max_layers);
        return false;
    }
    
    if (m_layers.find(layer_id) != m_layers.end()) {
        Logger::debug("Layer {} already exists", layer_id);
        return true; // Already exists, consider it success
    }
    
    auto layer = std::make_unique<Layer>();
    layer->id = layer_id;
    layer->visible = true;
    layer->z_order = layer_id; // Default z-order same as ID
    layer->blend_mode = BlendMode::ALPHA;
    layer->opacity = 1.0f;
    layer->dirty = true;
    layer->created_time = std::chrono::steady_clock::now();
    
    m_layers[layer_id] = std::move(layer);
    
    Logger::debug("Created layer {}", layer_id);
    return true;
}

bool LayerManager::deleteLayer(uint8_t layer_id) {
    if (layer_id == 0) {
        Logger::warning("Cannot delete layer 0 (default layer)");
        return false;
    }
    
    std::lock_guard<std::mutex> lock(m_layers_mutex);
    
    auto it = m_layers.find(layer_id);
    if (it == m_layers.end()) {
        Logger::warning("Layer {} does not exist", layer_id);
        return false;
    }
    
    // Clean up layer resources if needed
    auto& layer = it->second;
    if (layer->render_texture.id != 0) {
        UnloadRenderTexture(layer->render_texture);
    }
    
    m_layers.erase(it);
    
    Logger::debug("Deleted layer {}", layer_id);
    return true;
}

LayerManager::Layer* LayerManager::getLayer(uint8_t layer_id) {
    std::lock_guard<std::mutex> lock(m_layers_mutex);
    
    auto it = m_layers.find(layer_id);
    if (it != m_layers.end()) {
        return it->second.get();
    }
    
    return nullptr;
}

const LayerManager::Layer* LayerManager::getLayer(uint8_t layer_id) const {
    std::lock_guard<std::mutex> lock(m_layers_mutex);
    
    auto it = m_layers.find(layer_id);
    if (it != m_layers.end()) {
        return it->second.get();
    }
    
    return nullptr;
}

LayerManager::Layer* LayerManager::getOrCreateLayer(uint8_t layer_id) {
    {
        std::lock_guard<std::mutex> lock(m_layers_mutex);
        auto it = m_layers.find(layer_id);
        if (it != m_layers.end()) {
            return it->second.get();
        }
    }
    
    // Create layer if it doesn't exist
    if (createLayer(layer_id)) {
        std::lock_guard<std::mutex> lock(m_layers_mutex);
        return m_layers[layer_id].get();
    }
    
    return nullptr;
}

bool LayerManager::setLayerVisibility(uint8_t layer_id, bool visible) {
    auto* layer = getLayer(layer_id);
    if (!layer) {
        Logger::warning("Cannot set visibility for non-existent layer {}", layer_id);
        return false;
    }
    
    if (layer->visible != visible) {
        layer->visible = visible;
        layer->dirty = true;
        Logger::debug("Layer {} visibility set to {}", layer_id, visible);
    }
    
    return true;
}

bool LayerManager::setLayerOpacity(uint8_t layer_id, float opacity) {
    auto* layer = getLayer(layer_id);
    if (!layer) {
        Logger::warning("Cannot set opacity for non-existent layer {}", layer_id);
        return false;
    }
    
    opacity = std::clamp(opacity, 0.0f, 1.0f);
    
    if (std::abs(layer->opacity - opacity) > 0.001f) {
        layer->opacity = opacity;
        layer->dirty = true;
        Logger::debug("Layer {} opacity set to {:.3f}", layer_id, opacity);
    }
    
    return true;
}

bool LayerManager::setLayerBlendMode(uint8_t layer_id, BlendMode blend_mode) {
    auto* layer = getLayer(layer_id);
    if (!layer) {
        Logger::warning("Cannot set blend mode for non-existent layer {}", layer_id);
        return false;
    }
    
    if (layer->blend_mode != blend_mode) {
        layer->blend_mode = blend_mode;
        layer->dirty = true;
        Logger::debug("Layer {} blend mode set to {}", layer_id, static_cast<int>(blend_mode));
    }
    
    return true;
}

bool LayerManager::setLayerZOrder(uint8_t layer_id, float z_order) {
    auto* layer = getLayer(layer_id);
    if (!layer) {
        Logger::warning("Cannot set z-order for non-existent layer {}", layer_id);
        return false;
    }
    
    if (std::abs(layer->z_order - z_order) > 0.001f) {
        layer->z_order = z_order;
        m_needs_sort = true;
        Logger::debug("Layer {} z-order set to {:.3f}", layer_id, z_order);
    }
    
    return true;
}

bool LayerManager::clearLayer(uint8_t layer_id) {
    auto* layer = getLayer(layer_id);
    if (!layer) {
        Logger::warning("Cannot clear non-existent layer {}", layer_id);
        return false;
    }
    
    layer->object_count = 0;
    layer->vertex_count = 0;
    layer->dirty = true;
    layer->last_modified = std::chrono::steady_clock::now();
    
    // If using render texture, clear it
    if (layer->render_texture.id != 0) {
        BeginTextureMode(layer->render_texture);
        ClearBackground(BLANK); // Transparent
        EndTextureMode();
    }
    
    Logger::debug("Cleared layer {}", layer_id);
    return true;
}

void LayerManager::clearAllLayers() {
    std::lock_guard<std::mutex> lock(m_layers_mutex);
    
    for (auto& [layer_id, layer] : m_layers) {
        layer->object_count = 0;
        layer->vertex_count = 0;
        layer->dirty = true;
        layer->last_modified = std::chrono::steady_clock::now();
        
        if (layer->render_texture.id != 0) {
            BeginTextureMode(layer->render_texture);
            ClearBackground(BLANK);
            EndTextureMode();
        }
    }
    
    Logger::debug("Cleared all layers");
}

void LayerManager::markLayerDirty(uint8_t layer_id) {
    auto* layer = getLayer(layer_id);
    if (layer) {
        layer->dirty = true;
        layer->last_modified = std::chrono::steady_clock::now();
    }
}

void LayerManager::markLayerClean(uint8_t layer_id) {
    auto* layer = getLayer(layer_id);
    if (layer) {
        layer->dirty = false;
    }
}

bool LayerManager::isLayerDirty(uint8_t layer_id) const {
    const auto* layer = getLayer(layer_id);
    return layer ? layer->dirty : false;
}

bool LayerManager::isLayerVisible(uint8_t layer_id) const {
    const auto* layer = getLayer(layer_id);
    return layer ? layer->visible : false;
}

std::vector<uint8_t> LayerManager::getVisibleLayers() const {
    std::lock_guard<std::mutex> lock(m_layers_mutex);
    
    std::vector<uint8_t> visible_layers;
    for (const auto& [layer_id, layer] : m_layers) {
        if (layer->visible) {
            visible_layers.push_back(layer_id);
        }
    }
    
    return visible_layers;
}

std::vector<uint8_t> LayerManager::getAllLayers() const {
    std::lock_guard<std::mutex> lock(m_layers_mutex);
    
    std::vector<uint8_t> all_layers;
    for (const auto& [layer_id, layer] : m_layers) {
        all_layers.push_back(layer_id);
    }
    
    return all_layers;
}

std::vector<LayerManager::LayerInfo> LayerManager::getLayerInfos() const {
    std::lock_guard<std::mutex> lock(m_layers_mutex);
    
    std::vector<LayerInfo> infos;
    for (const auto& [layer_id, layer] : m_layers) {
        LayerInfo info;
        info.id = layer_id;
        info.visible = layer->visible;
        info.opacity = layer->opacity;
        info.blend_mode = layer->blend_mode;
        info.z_order = layer->z_order;
        info.object_count = layer->object_count;
        info.vertex_count = layer->vertex_count;
        info.dirty = layer->dirty;
        info.has_render_texture = (layer->render_texture.id != 0);
        
        infos.push_back(info);
    }
    
    return infos;
}

std::vector<LayerManager::Layer*> LayerManager::getLayersInRenderOrder() {
    std::lock_guard<std::mutex> lock(m_layers_mutex);
    
    std::vector<Layer*> layers;
    for (auto& [layer_id, layer] : m_layers) {
        if (layer->visible) {
            layers.push_back(layer.get());
        }
    }
    
    // Sort by z-order if needed
    if (m_needs_sort || layers.size() > 1) {
        std::sort(layers.begin(), layers.end(), [](const Layer* a, const Layer* b) {
            return a->z_order < b->z_order;
        });
        m_needs_sort = false;
    }
    
    return layers;
}

bool LayerManager::enableLayerCaching(uint8_t layer_id, uint32_t width, uint32_t height) {
    auto* layer = getLayer(layer_id);
    if (!layer) {
        Logger::warning("Cannot enable caching for non-existent layer {}", layer_id);
        return false;
    }
    
    // Clean up existing render texture if any
    if (layer->render_texture.id != 0) {
        UnloadRenderTexture(layer->render_texture);
    }
    
    // Create render texture for layer caching
    layer->render_texture = LoadRenderTexture(width, height);
    
    if (layer->render_texture.id == 0) {
        Logger::error("Failed to create render texture for layer {}", layer_id);
        return false;
    }
    
    layer->caching_enabled = true;
    layer->dirty = true;
    
    Logger::debug("Enabled caching for layer {} ({}x{})", layer_id, width, height);
    return true;
}

bool LayerManager::disableLayerCaching(uint8_t layer_id) {
    auto* layer = getLayer(layer_id);
    if (!layer) {
        Logger::warning("Cannot disable caching for non-existent layer {}", layer_id);
        return false;
    }
    
    if (layer->render_texture.id != 0) {
        UnloadRenderTexture(layer->render_texture);
        layer->render_texture = {0};
    }
    
    layer->caching_enabled = false;
    
    Logger::debug("Disabled caching for layer {}", layer_id);
    return true;
}

void LayerManager::updateLayerStats(uint8_t layer_id, uint32_t object_count, uint32_t vertex_count) {
    auto* layer = getLayer(layer_id);
    if (layer) {
        layer->object_count = object_count;
        layer->vertex_count = vertex_count;
        layer->last_modified = std::chrono::steady_clock::now();
    }
}

size_t LayerManager::getTotalObjectCount() const {
    std::lock_guard<std::mutex> lock(m_layers_mutex);
    
    size_t total = 0;
    for (const auto& [layer_id, layer] : m_layers) {
        total += layer->object_count;
    }
    
    return total;
}

size_t LayerManager::getTotalVertexCount() const {
    std::lock_guard<std::mutex> lock(m_layers_mutex);
    
    size_t total = 0;
    for (const auto& [layer_id, layer] : m_layers) {
        total += layer->vertex_count;
    }
    
    return total;
}

size_t LayerManager::getActiveLayerCount() const {
    std::lock_guard<std::mutex> lock(m_layers_mutex);
    
    size_t count = 0;
    for (const auto& [layer_id, layer] : m_layers) {
        if (layer->visible && layer->object_count > 0) {
            count++;
        }
    }
    
    return count;
}

size_t LayerManager::getDirtyLayerCount() const {
    std::lock_guard<std::mutex> lock(m_layers_mutex);
    
    size_t count = 0;
    for (const auto& [layer_id, layer] : m_layers) {
        if (layer->dirty) {
            count++;
        }
    }
    
    return count;
}

LayerManager::Stats LayerManager::getStats() const {
    std::lock_guard<std::mutex> lock(m_layers_mutex);
    
    Stats stats;
    stats.total_layers = m_layers.size();
    stats.visible_layers = 0;
    stats.cached_layers = 0;
    stats.dirty_layers = 0;
    stats.total_objects = 0;
    stats.total_vertices = 0;
    stats.memory_usage_mb = 0;
    
    for (const auto& [layer_id, layer] : m_layers) {
        if (layer->visible) {
            stats.visible_layers++;
        }
        
        if (layer->caching_enabled) {
            stats.cached_layers++;
        }
        
        if (layer->dirty) {
            stats.dirty_layers++;
        }
        
        stats.total_objects += layer->object_count;
        stats.total_vertices += layer->vertex_count;
        
        // Estimate memory usage for render textures
        if (layer->render_texture.id != 0) {
            uint32_t texture_memory = layer->render_texture.texture.width * 
                                    layer->render_texture.texture.height * 4; // RGBA
            stats.memory_usage_mb += texture_memory / (1024 * 1024);
        }
    }
    
    return stats;
}

void LayerManager::clear() {
    std::lock_guard<std::mutex> lock(m_layers_mutex);
    
    // Clean up render textures
    for (auto& [layer_id, layer] : m_layers) {
        if (layer->render_texture.id != 0) {
            UnloadRenderTexture(layer->render_texture);
        }
    }
    
    m_layers.clear();
    
    // Recreate default layer
    createLayer(0);
    
    Logger::debug("LayerManager cleared and reset");
}

void LayerManager::optimizeLayers() {
    std::lock_guard<std::mutex> lock(m_layers_mutex);
    
    auto now = std::chrono::steady_clock::now();
    std::vector<uint8_t> layers_to_remove;
    
    for (const auto& [layer_id, layer] : m_layers) {
        // Skip layer 0 (default layer)
        if (layer_id == 0) {
            continue;
        }
        
        // Remove empty layers that haven't been used for a while
        if (layer->object_count == 0) {
            auto idle_time = std::chrono::duration_cast<std::chrono::minutes>(
                now - layer->last_modified);
            
            if (idle_time.count() > 5) { // 5 minutes idle
                layers_to_remove.push_back(layer_id);
            }
        }
        
        // Disable caching for layers that are rarely used
        if (layer->caching_enabled && layer->object_count == 0) {
            auto idle_time = std::chrono::duration_cast<std::chrono::minutes>(
                now - layer->last_modified);
            
            if (idle_time.count() > 2) { // 2 minutes idle
                if (layer->render_texture.id != 0) {
                    UnloadRenderTexture(layer->render_texture);
                    layer->render_texture = {0};
                }
                layer->caching_enabled = false;
                Logger::debug("Disabled caching for idle layer {}", layer_id);
            }
        }
    }
    
    // Remove unused layers
    for (uint8_t layer_id : layers_to_remove) {
        auto it = m_layers.find(layer_id);
        if (it != m_layers.end()) {
            if (it->second->render_texture.id != 0) {
                UnloadRenderTexture(it->second->render_texture);
            }
            m_layers.erase(it);
            Logger::debug("Removed unused layer {}", layer_id);
        }
    }
    
    if (!layers_to_remove.empty()) {
        Logger::info("Optimized layers: removed {} unused layers", layers_to_remove.size());
    }
}

std::string LayerManager::getDebugInfo() const {
    std::lock_guard<std::mutex> lock(m_layers_mutex);
    
    std::stringstream ss;
    ss << "LayerManager Debug Info:\n";
    ss << "Total layers: " << m_layers.size() << "\n";
    ss << "Max layers: " << m_max_layers << "\n";
    ss << "Needs sort: " << (m_needs_sort ? "Yes" : "No") << "\n\n";
    
    for (const auto& [layer_id, layer] : m_layers) {
        ss << "Layer " << static_cast<int>(layer_id) << ":\n";
        ss << "  Visible: " << (layer->visible ? "Yes" : "No") << "\n";
        ss << "  Opacity: " << layer->opacity << "\n";
        ss << "  Z-order: " << layer->z_order << "\n";
        ss << "  Blend mode: " << static_cast<int>(layer->blend_mode) << "\n";
        ss << "  Objects: " << layer->object_count << "\n";
        ss << "  Vertices: " << layer->vertex_count << "\n";
        ss << "  Dirty: " << (layer->dirty ? "Yes" : "No") << "\n";
        ss << "  Caching: " << (layer->caching_enabled ? "Yes" : "No") << "\n";
        
        if (layer->render_texture.id != 0) {
            ss << "  Render texture: " << layer->render_texture.texture.width 
               << "x" << layer->render_texture.texture.height << "\n";
        }
        
        auto age = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - layer->created_time);
        ss << "  Age: " << age.count() << "s\n";
        
        auto last_modified = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - layer->last_modified);
        ss << "  Last modified: " << last_modified.count() << "s ago\n\n";
    }
    
    return ss.str();
}

// Helper function to convert blend mode to Raylib blend mode
int LayerManager::blendModeToRaylib(BlendMode mode) {
    switch (mode) {
        case BlendMode::ALPHA: return BLEND_ALPHA;
        case BlendMode::ADDITIVE: return BLEND_ADDITIVE;
        case BlendMode::MULTIPLIED: return BLEND_MULTIPLIED;
        case BlendMode::ADD_COLORS: return BLEND_ADD_COLORS;
        case BlendMode::SUBTRACT_COLORS: return BLEND_SUBTRACT_COLORS;
        case BlendMode::ALPHA_PREMULTIPLY: return BLEND_ALPHA_PREMULTIPLY;
        case BlendMode::CUSTOM: return BLEND_CUSTOM;
        default: return BLEND_ALPHA;
    }
}

} // namespace Kairos