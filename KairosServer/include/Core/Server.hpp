// KairosServer/src/Core/Server.hpp
#pragma once

#include "RaylibRenderer.hpp"
#include "NetworkManager.hpp"
#include "CommandProcessor.hpp"
#include "LayerManager.hpp"
#include "FontManager.hpp"
#include "Graphics/RenderCommand.hpp"
#include "Utils/Config.hpp"
#include "Utils/Logger.hpp"

#include <memory>
#include <thread>
#include <atomic>
#include <chrono>
#include <queue>
#include <unordered_map>

namespace Kairos {

/**
 * @brief Main Kairos graphics server
 * 
 * Orchestrates all subsystems:
 * - Network communication (NetworkManager)
 * - Graphics rendering (RaylibRenderer)
 * - Command processing (CommandProcessor)
 * - Layer management (LayerManager)
 * - Font management (FontManager)
 */
class Server {
public:
    struct Config {
        // Server identification
        std::string server_name = "Kairos Graphics Server";
        uint32_t server_version = PROTOCOL_VERSION;
        
        // Network configuration
        NetworkManager::Config network;
        
        // Rendering configuration
        RaylibRenderer::Config renderer;
        
        // Performance configuration
        struct {
            uint32_t target_fps = 60;
            uint32_t max_frame_time_ms = 33;        // 30fps minimum
            uint32_t command_batch_size = 1000;
            uint32_t render_thread_count = 1;       // Single-threaded rendering
            uint32_t network_thread_count = 2;
            
            bool enable_frame_pacing = true;
            bool enable_adaptive_quality = true;
            bool enable_vsync = true;
            bool enable_statistics = true;
            
            // Resource limits
            uint32_t max_textures = 1000;
            uint32_t max_fonts = 100;
            uint32_t max_render_commands_per_frame = 10000;
            size_t max_memory_usage_mb = 512;
        } performance;
        
        // Feature flags
        struct {
            bool enable_layers = true;
            bool enable_batching = true;
            bool enable_caching = true;
            bool enable_profiling = false;
            bool enable_debug_overlay = false;
            
            uint32_t max_layers = 255;
            bool layer_compositing = true;
            bool hardware_acceleration = true;
        } features;
        
        // Logging configuration
        struct {
            Logger::Level log_level = Logger::Level::Info;
            std::string log_file = "kairos_server.log";
            bool log_to_console = true;
            bool log_to_file = true;
            bool log_performance_stats = false;
        } logging;
    };
    
    struct Stats {
        // Server uptime
        std::chrono::steady_clock::time_point start_time;
        std::atomic<uint64_t> uptime_seconds{0};
        
        // Frame statistics
        std::atomic<uint64_t> frames_rendered{0};
        std::atomic<uint64_t> frames_dropped{0};
        std::atomic<float> current_fps{0.0f};
        std::atomic<float> avg_frame_time_ms{0.0f};
        std::atomic<float> cpu_usage_percent{0.0f};
        
        // Command statistics
        std::atomic<uint64_t> commands_received{0};
        std::atomic<uint64_t> commands_processed{0};
        std::atomic<uint64_t> commands_dropped{0};
        std::atomic<uint32_t> commands_queued{0};
        
        // Memory statistics
        std::atomic<uint32_t> memory_usage_mb{0};
        std::atomic<uint32_t> texture_memory_mb{0};
        std::atomic<uint32_t> buffer_memory_mb{0};
        
        // Client statistics (delegated from NetworkManager)
        std::atomic<uint32_t> active_clients{0};
        std::atomic<uint64_t> total_connections{0};
        std::atomic<uint64_t> messages_processed{0};
        
        // Layer statistics
        std::atomic<uint32_t> active_layers{0};
        std::atomic<uint32_t> cached_layers{0};
        std::atomic<uint32_t> dirty_layers{0};
        
        // Error statistics
        std::atomic<uint32_t> rendering_errors{0};
        std::atomic<uint32_t> network_errors{0};
        std::atomic<uint32_t> protocol_errors{0};
    };
    
    enum class State {
        STOPPED,
        INITIALIZING,
        RUNNING,
        STOPPING,
        ERROR
    };

public:
    explicit Server(const Config& config = Config{});
    ~Server();
    
    // Server lifecycle
    bool initialize();
    void run();
    void shutdown();
    void requestShutdown(const std::string& reason = "User request");
    
    // State management
    State getState() const { return m_state.load(); }
    bool isRunning() const { return m_state.load() == State::RUNNING; }
    std::string getStateString() const;
    
    // Configuration
    void setConfig(const Config& config);
    const Config& getConfig() const { return m_config; }
    bool reloadConfig(const std::string& config_file = "");
    
    // Statistics and monitoring
    const Stats& getStats() const { return m_stats; }
    void resetStats();
    std::string getStatusReport() const;
    void printPerformanceReport() const;
    
    // Client management (forwarded to NetworkManager)
    std::vector<uint32_t> getConnectedClients() const;
    bool disconnectClient(uint32_t client_id, const std::string& reason = "Server request");
    
    // Layer management
    void clearLayer(uint8_t layer_id);
    void clearAllLayers();
    void setLayerVisibility(uint8_t layer_id, bool visible);
    std::vector<uint8_t> getActiveLayers() const;
    
    // Resource management
    uint32_t loadFont(const std::string& font_path, uint32_t font_size);
    bool unloadFont(uint32_t font_id);
    uint32_t uploadTexture(uint32_t texture_id, uint32_t width, uint32_t height,
                          uint32_t format, const void* pixel_data, uint32_t data_size);
    bool deleteTexture(uint32_t texture_id);
    
    // Frame synchronization
    void waitForNextFrame();
    void sendFrameCallbacks();
    
    // Event broadcasting
    void broadcastInputEvent(const InputEvent& event);
    void sendInputEventToClient(uint32_t client_id, const InputEvent& event);
    
    // Debug and profiling
    void enableDebugOverlay(bool enabled);
    void savePerformanceProfile(const std::string& filename);
    
    // Signal handling
    static void signalHandler(int signal);
    void handleSignal(int signal);

private:
    // Main server loop
    void mainLoop();
    void processFrame();
    void processCommands();
    void renderFrame();
    void updateStatistics();
    
    // Subsystem management
    bool initializeSubsystems();
    void shutdownSubsystems();
    
    // Frame timing
    void enforceFrameRate();
    void measureFrameTime();
    std::chrono::microseconds getTargetFrameTime() const;
    
    // Command processing
    void processCommandBatch(const std::vector<RenderCommand>& commands);
    void handleHighPriorityCommands();
    void optimizeCommandOrder(std::vector<RenderCommand>& commands);
    
    // Resource monitoring
    void monitorSystemResources();
    void enforceResourceLimits();
    bool checkMemoryUsage();
    
    // Event callbacks (from NetworkManager)
    void onClientConnected(uint32_t client_id, const std::string& client_info);
    void onClientDisconnected(uint32_t client_id, const std::string& reason);
    void onCommandReceived(uint32_t client_id, RenderCommand&& command);
    void onNetworkError(const std::string& error_message, uint32_t client_id);
    
    // Performance optimization
    void adaptiveQualityControl();
    void adjustPerformanceSettings();
    
    // Debug and diagnostics
    void renderDebugOverlay();
    void logPerformanceMetrics();
    void detectPerformanceIssues();
    
    // Utility methods
    void setupSignalHandlers();
    void cleanupResources();
    std::string formatUptime() const;

private:
    Config m_config;
    Stats m_stats;
    std::atomic<State> m_state{State::STOPPED};
    
    // Subsystems
    std::unique_ptr<RaylibRenderer> m_renderer;
    std::unique_ptr<NetworkManager> m_network_manager;
    std::unique_ptr<CommandProcessor> m_command_processor;
    std::unique_ptr<LayerManager> m_layer_manager;
    std::unique_ptr<FontManager> m_font_manager;
    
    // Threading
    std::thread m_main_thread;
    std::atomic<bool> m_shutdown_requested{false};
    std::string m_shutdown_reason;
    
    // Command processing
    RenderCommandQueue m_command_queue;
    std::mutex m_high_priority_commands_mutex;
    std::vector<RenderCommand> m_high_priority_commands;
    
    // Frame timing
    std::chrono::steady_clock::time_point m_frame_start_time;
    std::chrono::steady_clock::time_point m_last_frame_time;
    std::chrono::microseconds m_accumulated_frame_time{0};
    uint32_t m_frame_count = 0;
    
    // Frame rate measurement
    std::queue<std::chrono::steady_clock::time_point> m_frame_times;
    static constexpr size_t FRAME_TIME_HISTORY_SIZE = 60;
    
    // Performance monitoring
    std::chrono::steady_clock::time_point m_last_stats_update;
    std::chrono::steady_clock::time_point m_last_performance_check;
    
    // Resource monitoring
    std::atomic<size_t> m_current_memory_usage{0};
    std::chrono::steady_clock::time_point m_last_memory_check;
    
    // Debug overlay
    bool m_debug_overlay_enabled = false;
    std::unordered_map<std::string, float> m_debug_metrics;
    
    // Error handling
    std::atomic<bool> m_has_critical_error{false};
    std::string m_last_error_message;
    
    // Global server instance for signal handling
    static Server* s_instance;
    static std::mutex s_instance_mutex;
};

/**
 * @brief Server builder for easy configuration
 */
class ServerBuilder {
public:
    ServerBuilder& withTcpPort(uint16_t port);
    ServerBuilder& withBindAddress(const std::string& address);
    ServerBuilder& withUnixSocket(const std::string& path);
    ServerBuilder& withWindowSize(uint32_t width, uint32_t height);
    ServerBuilder& withTargetFPS(uint32_t fps);
    ServerBuilder& withMaxClients(uint32_t max_clients);
    ServerBuilder& withMaxLayers(uint32_t max_layers);
    ServerBuilder& enableVSync(bool enabled = true);
    ServerBuilder& enableAntialiasing(bool enabled = true);
    ServerBuilder& enableLayerCaching(bool enabled = true);
    ServerBuilder& enableDebugMode(bool enabled = true);
    ServerBuilder& withLogLevel(Logger::Level level);
    ServerBuilder& withLogFile(const std::string& filename);
    
    std::unique_ptr<Server> build();

private:
    Server::Config m_config;
};

} // namespace Kairos