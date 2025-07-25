// KairosServer/include/Utils/Config.hpp
#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <variant>

namespace Kairos {

/**
 * @brief Configuration management for the server
 */
class Config {
public:
    using Value = std::variant<bool, int32_t, uint32_t, float, std::string>;
    using ConfigMap = std::unordered_map<std::string, Value>;
    
    struct NetworkConfig {
        std::string tcp_bind_address = "127.0.0.1";
        uint16_t tcp_port = 8080;
        bool enable_tcp = true;
        
        std::string unix_socket_path = "/tmp/kairos_server.sock";
        bool enable_unix_socket = true;
        
        uint32_t max_clients = 32;
        uint32_t max_connections_per_ip = 8;
        uint32_t client_timeout_seconds = 30;
        uint32_t handshake_timeout_seconds = 5;
        
        size_t receive_buffer_size = 64 * 1024;
        size_t send_buffer_size = 64 * 1024;
        size_t message_queue_size = 10000;
        
        bool enable_tcp_nodelay = true;
        bool enable_keepalive = true;
        bool enable_rate_limiting = true;
        uint32_t max_commands_per_second = 10000;
    };
    
    struct RendererConfig {
        uint32_t window_width = 1920;
        uint32_t window_height = 1080;
        uint32_t target_fps = 60;
        bool enable_vsync = true;
        bool enable_antialiasing = true;
        uint32_t msaa_samples = 4;
        bool fullscreen = false;
        bool hidden = false;
        std::string window_title = "Kairos Graphics Server";
        
        uint32_t max_batch_size = 10000;
        uint32_t vertex_buffer_size = 1024 * 1024;
        uint32_t texture_atlas_size = 2048;
        uint32_t max_layers = 255;
        bool layer_caching = true;
    };
    
    struct PerformanceConfig {
        uint32_t max_frame_time_ms = 33;
        uint32_t command_batch_size = 1000;
        uint32_t render_thread_count = 1;
        uint32_t network_thread_count = 2;
        
        bool enable_frame_pacing = true;
        bool enable_adaptive_quality = true;
        bool enable_statistics = true;
        
        uint32_t max_textures = 1000;
        uint32_t max_fonts = 100;
        uint32_t max_render_commands_per_frame = 10000;
        size_t max_memory_usage_mb = 512;
    };
    
    struct FeaturesConfig {
        bool enable_layers = true;
        bool enable_batching = true;
        bool enable_caching = true;
        bool enable_profiling = false;
        bool enable_debug_overlay = false;
        
        uint32_t max_layers = 255;
        bool layer_compositing = true;
        bool hardware_acceleration = true;
    };
    
    struct LoggingConfig {
        std::string log_level = "info";
        std::string log_file = "kairos_server.log";
        bool log_to_console = true;
        bool log_to_file = true;
        bool log_performance_stats = false;
        size_t max_log_file_size_mb = 100;
        uint32_t max_backup_files = 5;
    };

public:
    Config();
    ~Config() = default;
    
    // File operations
    bool loadFromFile(const std::string& filename);
    bool saveToFile(const std::string& filename) const;
    bool loadFromJson(const std::string& json_content);
    std::string saveToJson() const;
    
    // Command line parsing
    bool parseCommandLine(int argc, char* argv[]);
    void printUsage(const char* program_name) const;
    
    // Configuration access
    NetworkConfig& network() { return m_network; }
    const NetworkConfig& network() const { return m_network; }
    
    RendererConfig& renderer() { return m_renderer; }
    const RendererConfig& renderer() const { return m_renderer; }
    
    PerformanceConfig& performance() { return m_performance; }
    const PerformanceConfig& performance() const { return m_performance; }
    
    FeaturesConfig& features() { return m_features; }
    const FeaturesConfig& features() const { return m_features; }
    
    LoggingConfig& logging() { return m_logging; }
    const LoggingConfig& logging() const { return m_logging; }
    
    // Generic value access
    bool getBool(const std::string& key, bool default_value = false) const;
    int32_t getInt(const std::string& key, int32_t default_value = 0) const;
    uint32_t getUInt(const std::string& key, uint32_t default_value = 0) const;
    float getFloat(const std::string& key, float default_value = 0.0f) const;
    std::string getString(const std::string& key, const std::string& default_value = "") const;
    
    void setBool(const std::string& key, bool value);
    void setInt(const std::string& key, int32_t value);
    void setUInt(const std::string& key, uint32_t value);
    void setFloat(const std::string& key, float value);
    void setString(const std::string& key, const std::string& value);
    
    // Validation
    bool validate() const;
    std::vector<std::string> getValidationErrors() const;
    
    // Utilities
    void setDefaults();
    void merge(const Config& other);
    std::string getConfigSummary() const;

private:
    NetworkConfig m_network;
    RendererConfig m_renderer;
    PerformanceConfig m_performance;
    FeaturesConfig m_features;
    LoggingConfig m_logging;
    
    ConfigMap m_custom_values;
    
    // Helper methods
    bool parseJsonObject(const std::string& json);
    std::string generateJsonString() const;
    bool parseCommandLineArg(const std::string& arg, const std::string& value);
    void validateNetworkConfig() const;
    void validateRendererConfig() const;
    void validatePerformanceConfig() const;
};

/**
 * @brief Builder pattern for easy configuration
 */
class ConfigBuilder {
public:
    ConfigBuilder();
    
    // Network configuration
    ConfigBuilder& withTcpPort(uint16_t port);
    ConfigBuilder& withBindAddress(const std::string& address);
    ConfigBuilder& withUnixSocket(const std::string& path);
    ConfigBuilder& withMaxClients(uint32_t max_clients);
    ConfigBuilder& enableTcp(bool enabled = true);
    ConfigBuilder& enableUnixSocket(bool enabled = true);
    
    // Renderer configuration
    ConfigBuilder& withWindowSize(uint32_t width, uint32_t height);
    ConfigBuilder& withTargetFPS(uint32_t fps);
    ConfigBuilder& withWindowTitle(const std::string& title);
    ConfigBuilder& enableVSync(bool enabled = true);
    ConfigBuilder& enableAntialiasing(bool enabled = true);
    ConfigBuilder& enableFullscreen(bool enabled = true);
    ConfigBuilder& enableHiddenWindow(bool enabled = true);
    
    // Performance configuration
    ConfigBuilder& withMaxLayers(uint32_t max_layers);
    ConfigBuilder& withBatchSize(uint32_t batch_size);
    ConfigBuilder& withMemoryLimit(size_t limit_mb);
    ConfigBuilder& enableLayerCaching(bool enabled = true);
    ConfigBuilder& enableBatching(bool enabled = true);
    
    // Features configuration
    ConfigBuilder& enableProfiling(bool enabled = true);
    ConfigBuilder& enableDebugOverlay(bool enabled = true);
    ConfigBuilder& enableStatistics(bool enabled = true);
    
    // Logging configuration
    ConfigBuilder& withLogLevel(const std::string& level);
    ConfigBuilder& withLogFile(const std::string& filename);
    ConfigBuilder& enableConsoleLogging(bool enabled = true);
    ConfigBuilder& enableFileLogging(bool enabled = true);
    
    // Build final configuration
    Config build() const;

private:
    Config m_config;
};

} // namespace Kairos