// KairosServer/src/Utils/Logger.cpp
#include <Utils/Logger.hpp>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <chrono>

namespace Kairos {

// Static members
std::unique_ptr<Logger> Logger::s_instance;
std::mutex Logger::s_instance_mutex;

Logger& Logger::getInstance() {
    std::lock_guard<std::mutex> lock(s_instance_mutex);
    if (!s_instance) {
        s_instance = std::unique_ptr<Logger>(new Logger());
    }
    return *s_instance;
}

bool Logger::initialize(const Config& config) {
    auto& instance = getInstance();
    std::lock_guard<std::mutex> lock(instance.m_log_mutex);
    
    if (instance.m_initialized) {
        return true; // Already initialized
    }
    
    instance.m_config = config;
    
    // Open log file if needed
    if (config.log_to_file) {
        try {
            // Create directory if it doesn't exist
            std::filesystem::path log_path(config.log_file);
            std::filesystem::create_directories(log_path.parent_path());
            
            instance.m_log_file.open(config.log_file, std::ios::app);
            if (!instance.m_log_file.is_open()) {
                std::cerr << "Failed to open log file: " << config.log_file << std::endl;
                return false;
            }
        } catch (const std::exception& e) {
            std::cerr << "Exception opening log file: " << e.what() << std::endl;
            return false;
        }
    }
    
    instance.m_initialized = true;
    instance.log(Level::Info, "Logger initialized");
    
    return true;
}

void Logger::shutdown() {
    auto& instance = getInstance();
    std::lock_guard<std::mutex> lock(instance.m_log_mutex);
    
    if (!instance.m_initialized) {
        return;
    }
    
    instance.log(Level::Info, "Logger shutting down");
    
    if (instance.m_log_file.is_open()) {
        instance.m_log_file.close();
    }
    
    instance.m_initialized = false;
}

void Logger::setLevel(Level level) {
    auto& instance = getInstance();
    std::lock_guard<std::mutex> lock(instance.m_log_mutex);
    instance.m_config.log_level = level;
}

Logger::Level Logger::getLevel() {
    auto& instance = getInstance();
    std::lock_guard<std::mutex> lock(instance.m_log_mutex);
    return instance.m_config.log_level;
}

void Logger::debug(const std::string& message) {
    getInstance().log(Level::Debug, message);
}

void Logger::info(const std::string& message) {
    getInstance().log(Level::Info, message);
}

void Logger::warning(const std::string& message) {
    getInstance().log(Level::Warning, message);
}

void Logger::error(const std::string& message) {
    getInstance().log(Level::Error, message);
}

void Logger::flush() {
    auto& instance = getInstance();
    std::lock_guard<std::mutex> lock(instance.m_log_mutex);
    
    if (instance.m_log_file.is_open()) {
        instance.m_log_file.flush();
    }
    
    if (instance.m_config.log_to_console) {
        std::cout.flush();
        std::cerr.flush();
    }
}

void Logger::rotateLogs() {
    auto& instance = getInstance();
    std::lock_guard<std::mutex> lock(instance.m_log_mutex);
    
    if (!instance.m_config.log_to_file || !instance.m_log_file.is_open()) {
        return;
    }
    
    // Check file size
    instance.m_log_file.seekp(0, std::ios::end);
    size_t file_size = instance.m_log_file.tellp();
    
    if (file_size < instance.m_config.max_file_size_mb * 1024 * 1024) {
        return; // File not large enough to rotate
    }
    
    // Close current file
    instance.m_log_file.close();
    
    try {
        // Rotate backup files
        std::filesystem::path log_path(instance.m_config.log_file);
        std::string base_name = log_path.stem().string();
        std::string extension = log_path.extension().string();
        std::string dir = log_path.parent_path().string();
        
        // Remove oldest backup
        for (int i = instance.m_config.max_backup_files; i > 0; --i) {
            std::string old_file = dir + "/" + base_name + "." + std::to_string(i) + extension;
            std::string new_file = dir + "/" + base_name + "." + std::to_string(i + 1) + extension;
            
            if (i == instance.m_config.max_backup_files) {
                std::filesystem::remove(old_file);
            } else {
                if (std::filesystem::exists(old_file)) {
                    std::filesystem::rename(old_file, new_file);
                }
            }
        }
        
        // Move current log to .1
        std::string backup_file = dir + "/" + base_name + ".1" + extension;
        std::filesystem::rename(instance.m_config.log_file, backup_file);
        
        // Open new log file
        instance.m_log_file.open(instance.m_config.log_file, std::ios::app);
        instance.m_current_file_size = 0;
        
        instance.log(Level::Info, "Log file rotated");
        
    } catch (const std::exception& e) {
        std::cerr << "Failed to rotate log file: " << e.what() << std::endl;
        // Try to reopen original file
        instance.m_log_file.open(instance.m_config.log_file, std::ios::app);
    }
}

Logger::~Logger() {
    if (m_initialized && m_log_file.is_open()) {
        m_log_file.close();
    }
}

void Logger::log(Level level, const std::string& message) {
    std::lock_guard<std::mutex> lock(m_log_mutex);
    
    if (!m_initialized || level < m_config.log_level) {
        return;
    }
    
    std::string formatted_message = getCurrentTimestamp() + " [" + levelToString(level) + "] " + message;
    
    if (m_config.log_to_console) {
        writeToConsole(formatted_message);
    }
    
    if (m_config.log_to_file && m_log_file.is_open()) {
        writeToFile(formatted_message);
    }
    
    if (m_config.flush_immediately) {
        if (m_log_file.is_open()) {
            m_log_file.flush();
        }
        if (m_config.log_to_console) {
            std::cout.flush();
        }
    }
    
    // Check if file rotation is needed
    checkFileRotation();
}

std::string Logger::levelToString(Level level) const {
    switch (level) {
        case Level::Debug: return "DEBUG";
        case Level::Info: return "INFO";
        case Level::Warning: return "WARN";
        case Level::Error: return "ERROR";
        default: return "UNKNOWN";
    }
}

std::string Logger::getCurrentTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    
    return ss.str();
}

void Logger::writeToConsole(const std::string& message) {
    // Use cerr for warnings and errors, cout for others
    if (message.find("[WARN]") != std::string::npos || 
        message.find("[ERROR]") != std::string::npos) {
        std::cerr << message << std::endl;
    } else {
        std::cout << message << std::endl;
    }
}

void Logger::writeToFile(const std::string& message) {
    m_log_file << message << std::endl;
    m_current_file_size += message.length() + 1; // +1 for newline
}

void Logger::checkFileRotation() {
    if (m_config.log_to_file && 
        m_current_file_size > m_config.max_file_size_mb * 1024 * 1024) {
        rotateLogs();
    }
}

} // namespace Kairos