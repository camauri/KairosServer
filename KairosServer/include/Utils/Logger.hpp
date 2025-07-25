// KairosServer/include/Utils/Logger.hpp
#pragma once

#include <string>
#include <fstream>
#include <mutex>
#include <memory>
#include <sstream>

namespace Kairos {

/**
 * @brief Thread-safe logger with multiple output targets
 */
class Logger {
public:
    enum class Level {
        Debug = 0,
        Info = 1,
        Warning = 2,
        Error = 3
    };
    
    struct Config {
        Level log_level = Level::Info;
        bool log_to_console = true;
        bool log_to_file = true;
        std::string log_file = "kairos_server.log";
        bool flush_immediately = false;
        size_t max_file_size_mb = 100;
        uint32_t max_backup_files = 5;
    };

public:
    // Singleton access
    static Logger& getInstance();
    
    // Configuration
    static bool initialize(const Config& config = Config{});
    static void shutdown();
    static void setLevel(Level level);
    static Level getLevel();
    
    // Logging methods
    static void debug(const std::string& message);
    static void info(const std::string& message);
    static void warning(const std::string& message);
    static void error(const std::string& message);
    
    // Template logging with formatting
    template<typename... Args>
    static void debug(const std::string& format, Args&&... args) {
        getInstance().log(Level::Debug, format, std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    static void info(const std::string& format, Args&&... args) {
        getInstance().log(Level::Info, format, std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    static void warning(const std::string& format, Args&&... args) {
        getInstance().log(Level::Warning, format, std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    static void error(const std::string& format, Args&&... args) {
        getInstance().log(Level::Error, format, std::forward<Args>(args)...);
    }
    
    // File management
    static void flush();
    static void rotateLogs();
    
private:
    Logger() = default;
    ~Logger();
    
    // Internal logging
    void log(Level level, const std::string& message);
    
    template<typename... Args>
    void log(Level level, const std::string& format, Args&&... args) {
        if (level < m_config.log_level) {
            return;
        }
        
        std::string formatted = formatMessage(format, std::forward<Args>(args)...);
        log(level, formatted);
    }
    
    // Message formatting
    template<typename... Args>
    std::string formatMessage(const std::string& format, Args&&... args) {
        // Simple sprintf-style formatting
        // In a real implementation, use fmt library or similar
        return format; // Simplified for now
    }
    
    // Utilities
    std::string levelToString(Level level) const;
    std::string getCurrentTimestamp() const;
    void writeToConsole(const std::string& message);
    void writeToFile(const std::string& message);
    void checkFileRotation();
    
private:
    static std::unique_ptr<Logger> s_instance;
    static std::mutex s_instance_mutex;
    
    Config m_config;
    std::ofstream m_log_file;
    std::mutex m_log_mutex;
    bool m_initialized = false;
    size_t m_current_file_size = 0;
};

// Convenience macros for common logging patterns
#define KAIROS_LOG_DEBUG(msg) Kairos::Logger::debug(msg)
#define KAIROS_LOG_INFO(msg) Kairos::Logger::info(msg)
#define KAIROS_LOG_WARNING(msg) Kairos::Logger::warning(msg)
#define KAIROS_LOG_ERROR(msg) Kairos::Logger::error(msg)

} // namespace Kairos