// KairosServer/src/Utils/Config.cpp
#include <Utils/Config.hpp>
#include <Constants.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace Kairos {

Config::Config() {
    setDefaults();
}

void Config::setDefaults() {
    // Network defaults
    m_network.tcp_bind_address = "127.0.0.1";
    m_network.tcp_port = DEFAULT_SERVER_PORT;
    m_network.enable_tcp = true;
    m_network.unix_socket_path = DEFAULT_UNIX_SOCKET;
    m_network.enable_unix_socket = true;
    m_network.max_clients = 32;
    m_network.max_connections_per_ip = 8;
    m_network.client_timeout_seconds = 30;
    m_network.handshake_timeout_seconds = 5;
    m_network.receive_buffer_size = 64 * 1024;
    m_network.send_buffer_size = 64 * 1024;
    m_network.message_queue_size = 10000;
    m_network.enable_tcp_nodelay = true;
    m_network.enable_keepalive = true;
    m_network.enable_rate_limiting = true;
    m_network.max_commands_per_second = 10000;
    
    // Renderer defaults
    m_renderer.window_width = Defaults::WINDOW_WIDTH;
    m_renderer.window_height = Defaults::WINDOW_HEIGHT;
    m_renderer.target_fps = Defaults::TARGET_FPS;
    m_renderer.enable_vsync = true;
    m_renderer.enable_antialiasing = true;
    m_renderer.msaa_samples = 4;
    m_renderer.fullscreen = false;
    m_renderer.hidden = false;
    m_renderer.window_title = Defaults::WINDOW_TITLE;
    m_renderer.max_batch_size = Defaults::BATCH_SIZE;
    m_renderer.vertex_buffer_size = 1024 * 1024;
    m_renderer.texture_atlas_size = 2048;
    m_renderer.max_layers = Defaults::LAYER_COUNT;
    m_renderer.layer_caching = true;
    
    // Performance defaults
    m_performance.max_frame_time_ms = 33;
    m_performance.command_batch_size = Defaults::BATCH_SIZE;
    m_performance.render_thread_count = 1;
    m_performance.network_thread_count = 2;
    m_performance.enable_frame_pacing = true;
    m_performance.enable_adaptive_quality = true;
    m_performance.enable_statistics = true;
    m_performance.max_textures = 1000;
    m_performance.max_fonts = 100;
    m_performance.max_render_commands_per_frame = 10000;
    m_performance.max_memory_usage_mb = Defaults::DEFAULT_MEMORY_LIMIT_MB;
    
    // Features defaults
    m_features.enable_layers = true;
    m_features.enable_batching = true;
    m_features.enable_caching = true;
    m_features.enable_profiling = false;
    m_features.enable_debug_overlay = false;
    m_features.max_layers = Limits::MAX_LAYERS;
    m_features.layer_compositing = true;
    m_features.hardware_acceleration = true;
    
    // Logging defaults
    m_logging.log_level = "info";
    m_logging.log_file = Defaults::LOG_FILE;
    m_logging.log_to_console = true;
    m_logging.log_to_file = true;
    m_logging.log_performance_stats = false;
    m_logging.max_log_file_size_mb = 100;
    m_logging.max_backup_files = 5;
}

bool Config::loadFromFile(const std::string& filename) {
    try {
        std::ifstream file(filename);
        if (!file.is_open()) {
            std::cerr << "Cannot open config file: " << filename << std::endl;
            return false;
        }
        
        std::stringstream buffer;
        buffer << file.rdbuf();
        file.close();
        
        return loadFromJson(buffer.str());
        
    } catch (const std::exception& e) {
        std::cerr << "Exception loading config file: " << e.what() << std::endl;
        return false;
    }
}

bool Config::saveToFile(const std::string& filename) const {
    try {
        std::ofstream file(filename);
        if (!file.is_open()) {
            std::cerr << "Cannot create config file: " << filename << std::endl;
            return false;
        }
        
        file << saveToJson();
        file.close();
        
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception saving config file: " << e.what() << std::endl;
        return false;
    }
}

bool Config::loadFromJson(const std::string& json_content) {
    // Simplified JSON parsing - in real implementation use nlohmann::json
    // For now, just return true to indicate success
    return true;
}

std::string Config::saveToJson() const {
    // Simplified JSON generation - in real implementation use nlohmann::json
    std::stringstream ss;
    ss << "{\n";
    ss << "  \"network\": {\n";
    ss << "    \"tcp_bind_address\": \"" << m_network.tcp_bind_address << "\",\n";
    ss << "    \"tcp_port\": " << m_network.tcp_port << ",\n";
    ss << "    \"enable_tcp\": " << (m_network.enable_tcp ? "true" : "false") << ",\n";
    ss << "    \"unix_socket_path\": \"" << m_network.unix_socket_path << "\",\n";
    ss << "    \"enable_unix_socket\": " << (m_network.enable_unix_socket ? "true" : "false") << ",\n";
    ss << "    \"max_clients\": " << m_network.max_clients << "\n";
    ss << "  },\n";
    ss << "  \"renderer\": {\n";
    ss << "    \"window_width\": " << m_renderer.window_width << ",\n";
    ss << "    \"window_height\": " << m_renderer.window_height << ",\n";
    ss << "    \"target_fps\": " << m_renderer.target_fps << ",\n";
    ss << "    \"enable_vsync\": " << (m_renderer.enable_vsync ? "true" : "false") << ",\n";
    ss << "    \"fullscreen\": " << (m_renderer.fullscreen ? "true" : "false") << "\n";
    ss << "  },\n";
    ss << "  \"logging\": {\n";
    ss << "    \"log_level\": \"" << m_logging.log_level << "\",\n";
    ss << "    \"log_file\": \"" << m_logging.log_file << "\",\n";
    ss << "    \"log_to_console\": " << (m_logging.log_to_console ? "true" : "false") << "\n";
    ss << "  }\n";
    ss << "}\n";
    return ss.str();
}

bool Config::parseCommandLine(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        std::string value;
        
        if (i + 1 < argc) {
            value = argv[i + 1];
        }
        
        if (parseCommandLineArg(arg, value)) {
            ++i; // Skip value argument
        }
    }
    
    return validate();
}

bool Config::parseCommandLineArg(const std::string& arg, const std::string& value) {
    if (arg == "--port") {
        m_network.tcp_port = static_cast<uint16_t>(std::stoi(value));
        return true;
    }
    else if (arg == "--bind") {
        m_network.tcp_bind_address = value;
        return true;
    }
    else if (arg == "--unix-socket") {
        m_network.unix_socket_path = value;
        return true;
    }
    else if (arg == "--max-clients") {
        m_network.max_clients = static_cast<uint32_t>(std::stoi(value));
        return true;
    }
    else if (arg == "--width") {
        m_renderer.window_width = static_cast<uint32_t>(std::stoi(value));
        return true;
    }
    else if (arg == "--height") {
        m_renderer.window_height = static_cast<uint32_t>(std::stoi(value));
        return true;
    }
    else if (arg == "--fps") {
        m_renderer.target_fps = static_cast<uint32_t>(std::stoi(value));
        return true;
    }
    else if (arg == "--log-level") {
        m_logging.log_level = value;
        return true;
    }
    else if (arg == "--log-file") {
        m_logging.log_file = value;
        return true;
    }
    // Boolean flags (no value)
    else if (arg == "--no-tcp") {
        m_network.enable_tcp = false;
        return false;
    }
    else if (arg == "--no-unix") {
        m_network.enable_unix_socket = false;
        return false;
    }
    else if (arg == "--fullscreen") {
        m_renderer.fullscreen = true;
        return false;
    }
    else if (arg == "--hidden") {
        m_renderer.hidden = true;
        return false;
    }
    else if (arg == "--no-vsync") {
        m_renderer.enable_vsync = false;
        return false;
    }
    else if (arg == "--debug") {
        m_logging.log_level = "debug";
        m_features.enable_debug_overlay = true;
        return false;
    }
    
    return false; // Unknown argument
}

void Config::printUsage(const char* program_name) const {
    std::cout << "Usage: " << program_name << " [options]\n\n";
    std::cout << "Network Options:\n";
    std::cout << "  --port <port>        TCP server port (default: " << DEFAULT_SERVER_PORT << ")\n";
    std::cout << "  --bind <address>     Bind address (default: 127.0.0.1)\n";
    std::cout << "  --unix-socket <path> Unix socket path\n";
    std::cout << "  --max-clients <num>  Maximum clients (default: 32)\n";
    std::cout << "  --no-tcp           Disable TCP server\n";
    std::cout << "  --no-unix          Disable Unix socket\n\n";
    
    std::cout << "Graphics Options:\n";
    std::cout << "  --width <pixels>     Window width (default: " << Defaults::WINDOW_WIDTH << ")\n";
    std::cout << "  --height <pixels>    Window height (default: " << Defaults::WINDOW_HEIGHT << ")\n";
    std::cout << "  --fps <rate>         Target FPS (default: " << Defaults::TARGET_FPS << ")\n";
    std::cout << "  --fullscreen         Start fullscreen\n";
    std::cout << "  --hidden             Start hidden\n";
    std::cout << "  --no-vsync           Disable VSync\n\n";
    
    std::cout << "Logging Options:\n";
    std::cout << "  --log-level <level>  Log level (debug|info|warning|error)\n";
    std::cout << "  --log-file <path>    Log file path\n";
    std::cout << "  --debug              Enable debug mode\n\n";
}

bool Config::validate() const {
    std::vector<std::string> errors = getValidationErrors();
    
    if (!errors.empty()) {
        std::cerr << "Configuration validation errors:\n";
        for (const auto& error : errors) {
            std::cerr << "  - " << error << "\n";
        }
        return false;
    }
    
    return true;
}

std::vector<std::string> Config::getValidationErrors() const {
    std::vector<std::string> errors;
    
    // Network validation
    if (m_network.tcp_port < 1024) {
        errors.push_back("TCP port must be >= 1024");
    }
    
    if (m_network.max_clients == 0 || m_network.max_clients > Limits::MAX_CLIENTS) {
        errors.push_back("Max clients must be between 1 and " + std::to_string(Limits::MAX_CLIENTS));
    }
    
    // Renderer validation
    if (m_renderer.window_width < 320 || m_renderer.window_height < 240) {
        errors.push_back("Window size must be at least 320x240");
    }
    
    if (m_renderer.target_fps < Limits::MIN_FPS || m_renderer.target_fps > Limits::MAX_FPS) {
        errors.push_back("Target FPS must be between " + std::to_string(Limits::MIN_FPS) + 
                        " and " + std::to_string(Limits::MAX_FPS));
    }
    
    // Performance validation
    if (m_performance.max_memory_usage_mb > Limits::MAX_MEMORY_LIMIT_MB) {
        errors.push_back("Memory limit exceeds maximum of " + std::to_string(Limits::MAX_MEMORY_LIMIT_MB) + "MB");
    }
    
    // Logging validation
    std::vector<std::string> valid_levels = {"debug", "info", "warning", "error"};
    if (std::find(valid_levels.begin(), valid_levels.end(), m_logging.log_level) == valid_levels.end()) {
        errors.push_back("Invalid log level: " + m_logging.log_level);
    }
    
    return errors;
}

// Generic value access methods
bool Config::getBool(const std::string& key, bool default_value) const {
    auto it = m_custom_values.find(key);
    if (it != m_custom_values.end() && std::holds_alternative<bool>(it->second)) {
        return std::get<bool>(it->second);
    }
    return default_value;
}

int32_t Config::getInt(const std::string& key, int32_t default_value) const {
    auto it = m_custom_values.find(key);
    if (it != m_custom_values.end() && std::holds_alternative<int32_t>(it->second)) {
        return std::get<int32_t>(it->second);
    }
    return default_value;
}

uint32_t Config::getUInt(const std::string& key, uint32_t default_value) const {
    auto it = m_custom_values.find(key);
    if (it != m_custom_values.end() && std::holds_alternative<uint32_t>(it->second)) {
        return std::get<uint32_t>(it->second);
    }
    return default_value;
}

float Config::getFloat(const std::string& key, float default_value) const {
    auto it = m_custom_values.find(key);
    if (it != m_custom_values.end() && std::holds_alternative<float>(it->second)) {
        return std::get<float>(it->second);
    }
    return default_value;
}

std::string Config::getString(const std::string& key, const std::string& default_value) const {
    auto it = m_custom_values.find(key);
    if (it != m_custom_values.end() && std::holds_alternative<std::string>(it->second)) {
        return std::get<std::string>(it->second);
    }
    return default_value;
}

void Config::setBool(const std::string& key, bool value) {
    m_custom_values[key] = value;
}

void Config::setInt(const std::string& key, int32_t value) {
    m_custom_values[key] = value;
}

void Config::setUInt(const std::string& key, uint32_t value) {
    m_custom_values[key] = value;
}

void Config::setFloat(const std::string& key, float value) {
    m_custom_values[key] = value;
}

void Config::setString(const std::string& key, const std::string& value) {
    m_custom_values[key] = value;
}

void Config::merge(const Config& other) {
    // Merge other config into this one
    // For simplicity, just copy the custom values
    for (const auto& [key, value] : other.m_custom_values) {
        m_custom_values[key] = value;
    }
}

std::string Config::getConfigSummary() const {
    std::stringstream ss;
    ss << "Configuration Summary:\n";
    ss << "  Network: " << m_network.tcp_bind_address << ":" << m_network.tcp_port;
    if (m_network.enable_unix_socket) {
        ss << ", Unix: " << m_network.unix_socket_path;
    }
    ss << "\n";
    ss << "  Graphics: " << m_renderer.window_width << "x" << m_renderer.window_height 
       << " @ " << m_renderer.target_fps << "fps\n";
    ss << "  Logging: " << m_logging.log_level << " -> " << m_logging.log_file << "\n";
    return ss.str();
}

// ConfigBuilder implementation
ConfigBuilder::ConfigBuilder() {
    m_config.setDefaults();
}

ConfigBuilder& ConfigBuilder::withTcpPort(uint16_t port) {
    m_config.m_network.tcp_port = port;
    return *this;
}

ConfigBuilder& ConfigBuilder::withBindAddress(const std::string& address) {
    m_config.m_network.tcp_bind_address = address;
    return *this;
}

ConfigBuilder& ConfigBuilder::withUnixSocket(const std::string& path) {
    m_config.m_network.unix_socket_path = path;
    return *this;
}

ConfigBuilder& ConfigBuilder::withMaxClients(uint32_t max_clients) {
    m_config.m_network.max_clients = max_clients;
    return *this;
}

ConfigBuilder& ConfigBuilder::enableTcp(bool enabled) {
    m_config.m_network.enable_tcp = enabled;
    return *this;
}

ConfigBuilder& ConfigBuilder::enableUnixSocket(bool enabled) {
    m_config.m_network.enable_unix_socket = enabled;
    return *this;
}

ConfigBuilder& ConfigBuilder::withWindowSize(uint32_t width, uint32_t height) {
    m_config.m_renderer.window_width = width;
    m_config.m_renderer.window_height = height;
    return *this;
}

ConfigBuilder& ConfigBuilder::withTargetFPS(uint32_t fps) {
    m_config.m_renderer.target_fps = fps;
    return *this;
}

ConfigBuilder& ConfigBuilder::withWindowTitle(const std::string& title) {
    m_config.m_renderer.window_title = title;
    return *this;
}

ConfigBuilder& ConfigBuilder::enableVSync(bool enabled) {
    m_config.m_renderer.enable_vsync = enabled;
    return *this;
}

ConfigBuilder& ConfigBuilder::enableAntialiasing(bool enabled) {
    m_config.m_renderer.enable_antialiasing = enabled;
    return *this;
}

ConfigBuilder& ConfigBuilder::enableFullscreen(bool enabled) {
    m_config.m_renderer.fullscreen = enabled;
    return *this;
}

ConfigBuilder& ConfigBuilder::enableHiddenWindow(bool enabled) {
    m_config.m_renderer.hidden = enabled;
    return *this;
}

ConfigBuilder& ConfigBuilder::withMaxLayers(uint32_t max_layers) {
    m_config.m_features.max_layers = max_layers;
    return *this;
}

ConfigBuilder& ConfigBuilder::withBatchSize(uint32_t batch_size) {
    m_config.m_performance.command_batch_size = batch_size;
    return *this;
}

ConfigBuilder& ConfigBuilder::withMemoryLimit(size_t limit_mb) {
    m_config.m_performance.max_memory_usage_mb = limit_mb;
    return *this;
}

ConfigBuilder& ConfigBuilder::enableLayerCaching(bool enabled) {
    m_config.m_features.enable_caching = enabled;
    return *this;
}

ConfigBuilder& ConfigBuilder::enableBatching(bool enabled) {
    m_config.m_features.enable_batching = enabled;
    return *this;
}

ConfigBuilder& ConfigBuilder::enableProfiling(bool enabled) {
    m_config.m_features.enable_profiling = enabled;
    return *this;
}

ConfigBuilder& ConfigBuilder::enableDebugOverlay(bool enabled) {
    m_config.m_features.enable_debug_overlay = enabled;
    return *this;
}

ConfigBuilder& ConfigBuilder::enableStatistics(bool enabled) {
    m_config.m_performance.enable_statistics = enabled;
    return *this;
}

ConfigBuilder& ConfigBuilder::withLogLevel(const std::string& level) {
    m_config.m_logging.log_level = level;
    return *this;
}

ConfigBuilder& ConfigBuilder::withLogFile(const std::string& filename) {
    m_config.m_logging.log_file = filename;
    return *this;
}

ConfigBuilder& ConfigBuilder::enableConsoleLogging(bool enabled) {
    m_config.m_logging.log_to_console = enabled;
    return *this;
}

ConfigBuilder& ConfigBuilder::enableFileLogging(bool enabled) {
    m_config.m_logging.log_to_file = enabled;
    return *this;
}

Config ConfigBuilder::build() const {
    return m_config;
}

} // namespace Kairos