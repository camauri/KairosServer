// KairosServer/src/Core/Server.cpp
#include <Core/Server.hpp>
#include <Core/RaylibRenderer.hpp>
#include <Core/NetworkManager.hpp>
#include <Core/CommandProcessor.hpp>
#include <Core/LayerManager.hpp>
#include <Core/FontManager.hpp>
#include <Utils/Logger.hpp>
#include <iostream>
#include <sstream>

namespace Kairos {

// Static members for signal handling
Server* Server::s_instance = nullptr;
std::mutex Server::s_instance_mutex;

Server::Server(const Config& config) : m_config(config) {
    // Set global instance for signal handling
    {
        std::lock_guard<std::mutex> lock(s_instance_mutex);
        s_instance = this;
    }
    
    m_stats.start_time = std::chrono::steady_clock::now();
    
    Logger::info("Server created with configuration:");
    Logger::info(m_config.getConfigSummary());
}

Server::~Server() {
    if (m_state.load() != State::STOPPED) {
        shutdown();
    }
    
    // Clear global instance
    {
        std::lock_guard<std::mutex> lock(s_instance_mutex);
        if (s_instance == this) {
            s_instance = nullptr;
        }
    }
}

bool Server::initialize() {
    if (m_state.load() != State::STOPPED) {
        Logger::warning("Server already initialized");
        return false;
    }
    
    m_state = State::INITIALIZING;
    Logger::info("Initializing Kairos server...");
    
    try {
        if (!initializeSubsystems()) {
            m_state = State::ERROR;
            return false;
        }
        
        m_state = State::STOPPED; // Ready to run
        Logger::info("Server initialization completed successfully");
        return true;
        
    } catch (const std::exception& e) {
        Logger::error("Exception during server initialization: {}", e.what());
        m_state = State::ERROR;
        return false;
    }
}

void Server::run() {
    if (m_state.load() != State::STOPPED) {
        Logger::error("Cannot start server - invalid state: {}", getStateString());
        return;
    }
    
    Logger::info("Starting Kairos server main loop");
    m_state = State::RUNNING;
    
    try {
        mainLoop();
    } catch (const std::exception& e) {
        Logger::error("Exception in server main loop: {}", e.what());
        m_state = State::ERROR;
    }
    
    Logger::info("Server main loop ended");
}

void Server::shutdown() {
    Logger::info("Shutting down server...");
    
    m_state = State::STOPPING;
    m_shutdown_requested = true;
    
    // Wait for main thread to finish
    if (m_main_thread.joinable()) {
        m_main_thread.join();
    }
    
    // Shutdown subsystems
    shutdownSubsystems();
    
    m_state = State::STOPPED;
    Logger::info("Server shutdown complete");
}

void Server::requestShutdown(const std::string& reason) {
    Logger::info("Shutdown requested: {}", reason);
    m_shutdown_reason = reason;
    m_shutdown_requested = true;
}

std::string Server::getStateString() const {
    switch (m_state.load()) {
        case State::STOPPED: return "STOPPED";
        case State::INITIALIZING: return "INITIALIZING";
        case State::RUNNING: return "RUNNING";
        case State::STOPPING: return "STOPPING";
        case State::ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

void Server::setConfig(const Config& config) {
    if (m_state.load() == State::RUNNING) {
        Logger::warning("Cannot change configuration while server is running");
        return;
    }
    
    m_config = config;
    Logger::info("Server configuration updated");
}

bool Server::reloadConfig(const std::string& config_file) {
    if (!config_file.empty()) {
        Config new_config;
        if (!new_config.loadFromFile(config_file)) {
            Logger::error("Failed to load configuration from file: {}", config_file);
            return false;
        }
        setConfig(new_config);
    }
    
    // Apply configuration changes to subsystems
    if (m_renderer) {
        RaylibRenderer::Config renderer_config;
        renderer_config.window_width = m_config.renderer().window_width;
        renderer_config.window_height = m_config.renderer().window_height;
        renderer_config.target_fps = m_config.renderer().target_fps;
        renderer_config.enable_vsync = m_config.renderer().enable_vsync;
        renderer_config.enable_antialiasing = m_config.renderer().enable_antialiasing;
        renderer_config.layer_caching = m_config.renderer().layer_caching;
        m_renderer->setConfig(renderer_config);
    }
    
    if (m_network_manager) {
        NetworkManager::Config network_config;
        network_config.tcp_bind_address = m_config.network().tcp_bind_address;
        network_config.tcp_port = m_config.network().tcp_port;
        network_config.enable_tcp = m_config.network().enable_tcp;
        network_config.unix_socket_path = m_config.network().unix_socket_path;
        network_config.enable_unix_socket = m_config.network().enable_unix_socket;
        network_config.max_clients = m_config.network().max_clients;
        m_network_manager->setConfig(network_config);
    }
    
    return true;
}

const Server::Stats& Server::getStats() const {
    return m_stats;
}

void Server::resetStats() {
    m_stats = Stats{};
    m_stats.start_time = std::chrono::steady_clock::now();
    Logger::debug("Server statistics reset");
}

std::string Server::getStatusReport() const {
    std::stringstream ss;
    
    ss << "=== KAIROS SERVER STATUS ===\n";
    ss << "State: " << getStateString() << "\n";
    ss << "Uptime: " << formatUptime() << "\n";
    ss << "Configuration: " << m_config.getConfigSummary() << "\n";
    
    ss << "\nPerformance:\n";
    ss << "  Current FPS: " << m_stats.current_fps.load() << "\n";
    ss << "  Frames rendered: " << m_stats.frames_rendered.load() << "\n";
    ss << "  Frames dropped: " << m_stats.frames_dropped.load() << "\n";
    ss << "  Commands processed: " << m_stats.commands_processed.load() << "\n";
    ss << "  Memory usage: " << m_stats.memory_usage_mb.load() << " MB\n";
    
    ss << "\nClients:\n";
    ss << "  Active connections: " << m_stats.active_clients.load() << "\n";
    ss << "  Total connections: " << m_stats.total_connections.load() << "\n";
    ss << "  Messages processed: " << m_stats.messages_processed.load() << "\n";
    
    ss << "\nLayers:\n";
    ss << "  Active layers: " << m_stats.active_layers.load() << "\n";
    ss << "  Cached layers: " << m_stats.cached_layers.load() << "\n";
    ss << "  Dirty layers: " << m_stats.dirty_layers.load() << "\n";
    
    if (m_stats.rendering_errors.load() > 0 || m_stats.network_errors.load() > 0) {
        ss << "\nErrors:\n";
        ss << "  Rendering errors: " << m_stats.rendering_errors.load() << "\n";
        ss << "  Network errors: " << m_stats.network_errors.load() << "\n";
        ss << "  Protocol errors: " << m_stats.protocol_errors.load() << "\n";
    }
    
    ss << "============================\n";
    
    return ss.str();
}

void Server::printPerformanceReport() const {
    std::cout << getStatusReport() << std::endl;
}

std::vector<uint32_t> Server::getConnectedClients() const {
    if (m_network_manager) {
        return m_network_manager->getConnectedClients();
    }
    return {};
}

bool Server::disconnectClient(uint32_t client_id, const std::string& reason) {
    if (m_network_manager) {
        return m_network_manager->disconnectClient(client_id, reason);
    }
    return false;
}

void Server::clearLayer(uint8_t layer_id) {
    if (m_layer_manager) {
        m_layer_manager->clearLayer(layer_id);
    }
    if (m_renderer) {
        m_renderer->clearLayer(layer_id);
    }
}

void Server::clearAllLayers() {
    if (m_layer_manager) {
        m_layer_manager->clearAllLayers();
    }
    if (m_renderer) {
        m_renderer->clearAllLayers();
    }
}

void Server::setLayerVisibility(uint8_t layer_id, bool visible) {
    if (m_layer_manager) {
        m_layer_manager->setLayerVisibility(layer_id, visible);
    }
    if (m_renderer) {
        m_renderer->setLayerVisibility(layer_id, visible);
    }
}

std::vector<uint8_t> Server::getActiveLayers() const {
    if (m_layer_manager) {
        return m_layer_manager->getVisibleLayers();
    }
    return {};
}

uint32_t Server::loadFont(const std::string& font_path, uint32_t font_size) {
    if (m_font_manager) {
        return m_font_manager->loadFont(font_path, font_size);
    }
    return 0;
}

bool Server::unloadFont(uint32_t font_id) {
    if (m_font_manager) {
        return m_font_manager->unloadFont(font_id);
    }
    return false;
}

uint32_t Server::uploadTexture(uint32_t texture_id, uint32_t width, uint32_t height,
                              uint32_t format, const void* pixel_data, uint32_t data_size) {
    if (m_renderer) {
        return m_renderer->uploadTexture(texture_id, width, height, format, pixel_data, data_size);
    }
    return 0;
}

bool Server::deleteTexture(uint32_t texture_id) {
    if (m_renderer) {
        return m_renderer->deleteTexture(texture_id);
    }
    return false;
}

void Server::waitForNextFrame() {
    auto target_frame_time = getTargetFrameTime();
    auto elapsed = std::chrono::steady_clock::now() - m_frame_start_time;
    
    if (elapsed < target_frame_time) {
        std::this_thread::sleep_for(target_frame_time - elapsed);
    }
}

void Server::sendFrameCallbacks() {
    if (!m_network_manager) return;
    
    FrameCallback callback;
    callback.frame_number = static_cast<uint32_t>(m_stats.frames_rendered.load());
    callback.frame_time = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    callback.frame_rate = m_stats.current_fps.load();
    callback.dropped_frames = static_cast<uint32_t>(m_stats.frames_dropped.load());
    
    MessageHeader header;
    header.type = MessageType::FRAME_CALLBACK;
    header.data_size = sizeof(FrameCallback);
    
    m_network_manager->broadcastMessage(header, &callback);
}

void Server::broadcastInputEvent(const InputEvent& event) {
    if (!m_network_manager) return;
    
    MessageHeader header;
    header.type = MessageType::INPUT_EVENT;
    header.data_size = sizeof(InputEvent);
    
    m_network_manager->broadcastMessage(header, &event);
}

void Server::sendInputEventToClient(uint32_t client_id, const InputEvent& event) {
    if (!m_network_manager) return;
    
    m_network_manager->sendInputEvent(client_id, event);
}

void Server::enableDebugOverlay(bool enabled) {
    m_debug_overlay_enabled = enabled;
    if (enabled) {
        Logger::info("Debug overlay enabled");
    } else {
        Logger::info("Debug overlay disabled");
    }
}

void Server::savePerformanceProfile(const std::string& filename) {
    try {
        std::ofstream file(filename);
        if (!file.is_open()) {
            Logger::error("Cannot create performance profile file: {}", filename);
            return;
        }
        
        file << "# Kairos Server Performance Profile\n";
        file << "# Generated: " << std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count() << "\n\n";
        
        file << getStatusReport();
        
        // Add detailed metrics if available
        if (m_renderer) {
            const auto& renderer_stats = m_renderer->getStats();
            file << "\nRenderer Statistics:\n";
            file << "  Vertices rendered: " << renderer_stats.vertices_rendered << "\n";
            file << "  Draw calls issued: " << renderer_stats.draw_calls_issued << "\n";
            file << "  Textures uploaded: " << renderer_stats.textures_uploaded << "\n";
        }
        
        if (m_command_processor) {
            const auto& processor_stats = m_command_processor->getStats();
            file << "\nCommand Processor Statistics:\n";
            file << "  Commands received: " << processor_stats.commands_received.load() << "\n";
            file << "  Commands processed: " << processor_stats.commands_processed.load() << "\n";
            file << "  Commands dropped: " << processor_stats.commands_dropped.load() << "\n";
            file << "  Queue size: " << processor_stats.queue_size.load() << "\n";
        }
        
        file.close();
        Logger::info("Performance profile saved to: {}", filename);
        
    } catch (const std::exception& e) {
        Logger::error("Exception saving performance profile: {}", e.what());
    }
}

void Server::signalHandler(int signal) {
    std::lock_guard<std::mutex> lock(s_instance_mutex);
    if (s_instance) {
        s_instance->handleSignal(signal);
    }
}

void Server::handleSignal(int signal) {
    const char* signal_name = "Unknown";
    switch (signal) {
        case SIGINT: signal_name = "SIGINT"; break;
        case SIGTERM: signal_name = "SIGTERM"; break;
#ifndef _WIN32
        case SIGHUP: signal_name = "SIGHUP"; break;
#endif
    }
    
    Logger::info("Received signal {} ({})", signal, signal_name);
    requestShutdown("Signal received");
}

// Private methods implementation

void Server::mainLoop() {
    Logger::info("Entering main server loop");
    
    setupSignalHandlers();
    
    while (!m_shutdown_requested && m_state.load() == State::RUNNING) {
        try {
            processFrame();
            
            // Update statistics periodically
            updateStatistics();
            
            // Check for resource limits
            if (m_config.performance().enable_statistics) {
                monitorSystemResources();
            }
            
        } catch (const std::exception& e) {
            Logger::error("Exception in main loop frame: {}", e.what());
            m_stats.rendering_errors.fetch_add(1);
            
            // Continue processing unless it's a critical error
            if (m_stats.rendering_errors.load() > 100) {
                Logger::error("Too many rendering errors, stopping server");
                break;
            }
        }
    }
    
    Logger::info("Main server loop ended");
}

void Server::processFrame() {
    m_frame_start_time = std::chrono::steady_clock::now();
    
    // Begin rendering frame
    if (m_renderer) {
        m_renderer->beginFrame();
    }
    
    // Process incoming commands
    processCommands();
    
    // Render frame
    renderFrame();
    
    // End rendering frame
    if (m_renderer) {
        m_renderer->endFrame();
        
        // Check if window should close
        if (m_renderer->shouldClose()) {
            requestShutdown("Window close requested");
        }
    }
    
    // Send frame callbacks to clients
    if (m_config.features().enable_layers) {
        sendFrameCallbacks();
    }
    
    // Enforce frame rate
    if (m_config.performance().enable_frame_pacing) {
        enforceFrameRate();
    }
    
    // Measure frame time
    measureFrameTime();
    
    m_stats.frames_rendered.fetch_add(1);
}

void Server::processCommands() {
    if (!m_command_processor) return;
    
    // Process high priority commands first
    handleHighPriorityCommands();
    
    // Process regular command queue
    auto commands = m_command_queue.dequeueBatch(m_config.performance().command_batch_size);
    if (!commands.empty()) {
        optimizeCommandOrder(commands);
        m_command_processor->processCommandBatch(commands);
        m_stats.commands_processed.fetch_add(commands.size());
    }
}

void Server::renderFrame() {
    if (!m_renderer) return;
    
    // Render debug overlay if enabled
    if (m_debug_overlay_enabled) {
        renderDebugOverlay();
    }
    
    // Additional rendering logic would go here
}

void Server::updateStatistics() {
    static auto last_update = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    
    if (now - last_update >= std::chrono::seconds(1)) {
        // Update uptime
        auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - m_stats.start_time);
        m_stats.uptime_seconds.store(uptime.count());
        
        // Get stats from subsystems
        if (m_network_manager) {
            const auto& net_stats = m_network_manager->getStats();
            m_stats.active_clients.store(net_stats.active_connections.load());
            m_stats.total_connections.store(net_stats.total_connections.load());
            m_stats.messages_processed.store(net_stats.messages_received.load());
        }
        
        if (m_layer_manager) {
            const auto layer_stats = m_layer_manager->getStats();
            m_stats.active_layers.store(layer_stats.visible_layers);
            m_stats.cached_layers.store(layer_stats.cached_layers);
            m_stats.dirty_layers.store(layer_stats.dirty_layers);
        }
        
        if (m_renderer) {
            const auto& renderer_stats = m_renderer->getStats();
            m_stats.current_fps.store(renderer_stats.current_fps);
            m_stats.avg_frame_time_ms.store(renderer_stats.avg_frame_time_ms);
            m_stats.memory_usage_mb.store(renderer_stats.memory_usage_mb);
        }
        
        last_update = now;
        
        // Log performance metrics if enabled
        if (m_config.logging().log_performance_stats) {
            logPerformanceMetrics();
        }
    }
}

bool Server::initializeSubsystems() {
    Logger::info("Initializing server subsystems...");
    
    // Initialize renderer
    Logger::info("Initializing renderer...");
    RaylibRenderer::Config renderer_config;
    renderer_config.window_width = m_config.renderer().window_width;
    renderer_config.window_height = m_config.renderer().window_height;
    renderer_config.target_fps = m_config.renderer().target_fps;
    renderer_config.enable_vsync = m_config.renderer().enable_vsync;
    renderer_config.enable_antialiasing = m_config.renderer().enable_antialiasing;
    renderer_config.fullscreen = m_config.renderer().fullscreen;
    renderer_config.hidden = m_config.renderer().hidden;
    renderer_config.window_title = m_config.renderer().window_title;
    renderer_config.layer_caching = m_config.renderer().layer_caching;
    
    m_renderer = std::make_unique<RaylibRenderer>(renderer_config);
    if (!m_renderer->initialize()) {
        Logger::error("Failed to initialize renderer");
        return false;
    }
    
    // Initialize layer manager
    Logger::info("Initializing layer manager...");
    m_layer_manager = std::make_unique<LayerManager>(m_config.features().max_layers);
    
    // Initialize font manager
    Logger::info("Initializing font manager...");
    m_font_manager = std::make_unique<FontManager>();
    if (!m_font_manager->initialize()) {
        Logger::error("Failed to initialize font manager");
        return false;
    }
    
    // Initialize command processor
    Logger::info("Initializing command processor...");
    m_command_processor = std::make_unique<CommandProcessor>(*m_renderer, *m_layer_manager, *m_font_manager);
    if (!m_command_processor->initialize()) {
        Logger::error("Failed to initialize command processor");
        return false;
    }
    
    // Initialize network manager
    Logger::info("Initializing network manager...");
    NetworkManager::Config network_config;
    network_config.tcp_bind_address = m_config.network().tcp_bind_address;
    network_config.tcp_port = m_config.network().tcp_port;
    network_config.enable_tcp = m_config.network().enable_tcp;
    network_config.unix_socket_path = m_config.network().unix_socket_path;
    network_config.enable_unix_socket = m_config.network().enable_unix_socket;
    network_config.max_clients = m_config.network().max_clients;
    
    m_network_manager = std::make_unique<NetworkManager>(network_config);
    if (!m_network_manager->initialize()) {
        Logger::error("Failed to initialize network manager");
        return false;
    }
    
    // Set up network callbacks
    m_network_manager->setClientConnectedCallback(
        [this](uint32_t client_id, const std::string& client_info) {
            onClientConnected(client_id, client_info);
        });
    
    m_network_manager->setClientDisconnectedCallback(
        [this](uint32_t client_id, const std::string& reason) {
            onClientDisconnected(client_id, reason);
        });
    
    m_network_manager->setCommandReceivedCallback(
        [this](uint32_t client_id, RenderCommand&& command) {
            onCommandReceived(client_id, std::move(command));
        });
    
    m_network_manager->setErrorCallback(
        [this](const std::string& error_message, uint32_t client_id) {
            onNetworkError(error_message, client_id);
        });
    
    Logger::info("All subsystems initialized successfully");
    return true;
}

void Server::shutdownSubsystems() {
    Logger::info("Shutting down server subsystems...");
    
    if (m_network_manager) {
        m_network_manager->shutdown();
        m_network_manager.reset();
    }
    
    if (m_command_processor) {
        m_command_processor->shutdown();
        m_command_processor.reset();
    }
    
    if (m_font_manager) {
        m_font_manager.reset();
    }
    
    if (m_layer_manager) {
        m_layer_manager.reset();
    }
    
    if (m_renderer) {
        m_renderer->shutdown();
        m_renderer.reset();
    }
    
    Logger::info("All subsystems shut down");
}

std::chrono::microseconds Server::getTargetFrameTime() const {
    return std::chrono::microseconds(1000000 / m_config.renderer().target_fps);
}

void Server::enforceFrameRate() {
    waitForNextFrame();
}

void Server::measureFrameTime() {
    auto now = std::chrono::steady_clock::now();
    auto frame_time = std::chrono::duration_cast<std::chrono::microseconds>(now - m_frame_start_time);
    
    // Update frame time history
    m_frame_times.push(now);
    if (m_frame_times.size() > FRAME_TIME_HISTORY_SIZE) {
        m_frame_times.pop();
    }
    
    // Calculate FPS from history
    if (m_frame_times.size() >= 2) {
        auto time_span = now - m_frame_times.front();
        double seconds = std::chrono::duration<double>(time_span).count();
        m_stats.current_fps.store(static_cast<float>((m_frame_times.size() - 1) / seconds));
    }
    
    m_stats.avg_frame_time_ms.store(frame_time.count() / 1000.0f);
}

void Server::handleHighPriorityCommands() {
    std::lock_guard<std::mutex> lock(m_high_priority_commands_mutex);
    if (!m_high_priority_commands.empty()) {
        for (const auto& command : m_high_priority_commands) {
            if (m_command_processor) {
                m_command_processor->processCommand(command);
            }
        }
        m_high_priority_commands.clear();
    }
}

void Server::optimizeCommandOrder(std::vector<RenderCommand>& commands) {
    // Sort commands by layer first, then by type for optimal rendering
    std::sort(commands.begin(), commands.end(), [](const RenderCommand& a, const RenderCommand& b) {
        if (a.layer_id != b.layer_id) {
            return a.layer_id < b.layer_id;
        }
        return static_cast<int>(a.type) < static_cast<int>(b.type);
    });
}

void Server::monitorSystemResources() {
    // Monitor memory usage
    checkMemoryUsage();
    
    // Check for performance issues
    detectPerformanceIssues();
}

bool Server::checkMemoryUsage() {
    size_t current_usage = m_current_memory_usage.load();
    size_t limit = m_config.performance().max_memory_usage_mb * 1024 * 1024;
    
    if (current_usage > limit) {
        Logger::warning("Memory usage ({} MB) exceeds limit ({} MB)", 
                       current_usage / (1024 * 1024), limit / (1024 * 1024));
        
        // Try to free some memory
        if (m_font_manager) {
            m_font_manager->optimizeMemory();
        }
        if (m_layer_manager) {
            m_layer_manager->optimizeLayers();
        }
        
        return false;
    }
    
    return true;
}

void Server::onClientConnected(uint32_t client_id, const std::string& client_info) {
    Logger::info("Client {} connected: {}", client_id, client_info);
    m_stats.total_connections.fetch_add(1);
}

void Server::onClientDisconnected(uint32_t client_id, const std::string& reason) {
    Logger::info("Client {} disconnected: {}", client_id, reason);
}

void Server::onCommandReceived(uint32_t client_id, RenderCommand&& command) {
    // Set command metadata
    command.client_id = client_id;
    command.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    
    // Queue command for processing
    if (command.priority >= RenderCommand::Priority::HIGH) {
        std::lock_guard<std::mutex> lock(m_high_priority_commands_mutex);
        m_high_priority_commands.push_back(std::move(command));
    } else {
        m_command_queue.enqueue(std::move(command));
    }
    
    m_stats.commands_received.fetch_add(1);
}

void Server::onNetworkError(const std::string& error_message, uint32_t client_id) {
    Logger::error("Network error for client {}: {}", client_id, error_message);
    m_stats.network_errors.fetch_add(1);
}

void Server::renderDebugOverlay() {
    // Simple debug overlay rendering
    // In a real implementation, this would render performance metrics on screen
    m_debug_metrics["FPS"] = m_stats.current_fps.load();
    m_debug_metrics["Frame Time"] = m_stats.avg_frame_time_ms.load();
    m_debug_metrics["Commands"] = static_cast<float>(m_stats.commands_processed.load());
    m_debug_metrics["Clients"] = static_cast<float>(m_stats.active_clients.load());
}

void Server::logPerformanceMetrics() {
    Logger::debug("Performance: FPS={:.1f}, Frame={:.2f}ms, Cmds={}, Clients={}", 
                 m_stats.current_fps.load(), 
                 m_stats.avg_frame_time_ms.load(),
                 m_stats.commands_processed.load(),
                 m_stats.active_clients.load());
}

void Server::detectPerformanceIssues() {
    // Check for low FPS
    if (m_stats.current_fps.load() < m_config.renderer().target_fps * 0.8f) {
        static auto last_warning = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        
        if (now - last_warning > std::chrono::seconds(10)) {
            Logger::warning("Low FPS detected: {:.1f} (target: {})", 
                           m_stats.current_fps.load(), m_config.renderer().target_fps);
            last_warning = now;
        }
    }
    
    // Check for high frame time
    if (m_stats.avg_frame_time_ms.load() > m_config.performance().max_frame_time_ms) {
        static auto last_warning = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        
        if (now - last_warning > std::chrono::seconds(10)) {
            Logger::warning("High frame time detected: {:.2f}ms (limit: {}ms)", 
                           m_stats.avg_frame_time_ms.load(), 
                           m_config.performance().max_frame_time_ms);
            last_warning = now;
        }
    }
}

void Server::setupSignalHandlers() {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
#ifndef _WIN32
    std::signal(SIGHUP, signalHandler);
    std::signal(SIGPIPE, SIG_IGN);  // Ignore broken pipe
#endif
}

void Server::cleanupResources() {
    // Additional cleanup if needed
}

std::string Server::formatUptime() const {
    auto uptime_seconds = m_stats.uptime_seconds.load();
    auto hours = uptime_seconds / 3600;
    auto minutes = (uptime_seconds % 3600) / 60;
    auto seconds = uptime_seconds % 60;
    
    std::stringstream ss;
    ss << hours << "h " << minutes << "m " << seconds << "s";
    return ss.str();
}

} // namespace Kairos