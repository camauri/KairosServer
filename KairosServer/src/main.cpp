// KairosServer/src/main.cpp
#include <Core/Server.hpp>
#include <Utils/Logger.hpp>
#include <Utils/Config.hpp>
#include <Protocol.hpp>

#include <iostream>
#include <string>
#include <vector>
#include <csignal>
#include <cstdlib>
#include <memory>

#ifdef _WIN32
    #include <windows.h>
    #include <io.h>
#else
    #include <unistd.h>
    #include <sys/stat.h>
#endif

using namespace Kairos;

// Global server instance for signal handling
std::unique_ptr<Server> g_server;
std::mutex g_server_mutex;

void printBanner() {
    std::cout << R"(
===============================================================================
                     KAIROS RAYLIB GRAPHICS SERVER v1.0                      
                        High-Performance Graphics Server                       
===============================================================================
    )" << std::endl;
}

void printUsage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [options]\n\n";
    
    std::cout << "Network Options:\n";
    std::cout << "  --port <port>            TCP server port (default: " << DEFAULT_SERVER_PORT << ")\n";
    std::cout << "  --bind <address>         Bind address (default: 127.0.0.1)\n";
    std::cout << "  --unix-socket <path>     Unix socket path (default: " << DEFAULT_UNIX_SOCKET << ")\n";
    std::cout << "  --max-clients <count>    Maximum concurrent clients (default: 32)\n";
    std::cout << "  --no-tcp                Disable TCP server\n";
    std::cout << "  --no-unix               Disable Unix socket server\n\n";
    
    std::cout << "Graphics Options:\n";
    std::cout << "  --width <pixels>         Window width (default: 1920)\n";
    std::cout << "  --height <pixels>        Window height (default: 1080)\n";
    std::cout << "  --fps <rate>             Target frame rate (default: 60)\n";
    std::cout << "  --fullscreen             Start in fullscreen mode\n";
    std::cout << "  --hidden                 Start with hidden window\n";
    std::cout << "  --no-vsync              Disable VSync\n";
    std::cout << "  --no-antialiasing       Disable antialiasing\n\n";
    
    std::cout << "Performance Options:\n";
    std::cout << "  --max-layers <count>     Maximum layers (default: 255)\n";
    std::cout << "  --batch-size <size>      Command batch size (default: 1000)\n";
    std::cout << "  --no-caching            Disable layer caching\n";
    std::cout << "  --no-batching           Disable command batching\n";
    std::cout << "  --memory-limit <MB>      Memory limit in MB (default: 512)\n\n";
    
    std::cout << "Debugging Options:\n";
    std::cout << "  --debug                  Enable debug mode\n";
    std::cout << "  --log-level <level>      Log level (debug|info|warning|error)\n";
    std::cout << "  --log-file <path>        Log file path (default: kairos_server.log)\n";
    std::cout << "  --no-log-file           Disable file logging\n";
    std::cout << "  --profile               Enable performance profiling\n";
    std::cout << "  --debug-overlay         Show debug overlay\n\n";
    
    std::cout << "Configuration Options:\n";
    std::cout << "  --config <file>          Load configuration from file\n";
    std::cout << "  --save-config <file>     Save current config to file\n\n";
    
    std::cout << "System Options:\n";
    std::cout << "  --daemon                 Run as daemon (Linux/macOS only)\n";
    std::cout << "  --pid-file <path>        Write PID to file\n";
    std::cout << "  --help                   Show this help message\n";
    std::cout << "  --version                Show version information\n\n";
    
    std::cout << "Examples:\n";
    std::cout << "  " << program_name << " --port 8080 --width 1920 --height 1080\n";
    std::cout << "  " << program_name << " --unix-socket /tmp/kairos.sock --no-tcp\n";
    std::cout << "  " << program_name << " --config server.json --debug\n";
    std::cout << "  " << program_name << " --fullscreen --max-clients 16 --fps 120\n\n";
    
    std::cout << "For more information, visit: https://github.com/your-org/KairosRaylib\n";
}

void printVersion() {
    std::cout << "Kairos Raylib Graphics Server\n";
    std::cout << "Version: 1.0.0\n";
    std::cout << "Protocol Version: " << PROTOCOL_VERSION << "\n";
    std::cout << "Build Date: " << __DATE__ << " " << __TIME__ << "\n";
    
#ifdef _WIN32
    std::cout << "Platform: Windows\n";
#elif defined(__linux__)
    std::cout << "Platform: Linux\n";
#elif defined(__APPLE__)
    std::cout << "Platform: macOS\n";
#else
    std::cout << "Platform: Unknown\n";
#endif
    
    std::cout << "Features: TCP, Unix Sockets, Layers, Batching, Caching\n";
}

void signalHandler(int signal) {
    std::lock_guard<std::mutex> lock(g_server_mutex);
    
    const char* signal_name = "Unknown";
    switch (signal) {
        case SIGINT: signal_name = "SIGINT"; break;
        case SIGTERM: signal_name = "SIGTERM"; break;
#ifndef _WIN32
        case SIGHUP: signal_name = "SIGHUP"; break;
        case SIGPIPE: signal_name = "SIGPIPE"; break;
#endif
    }
    
    std::cout << "\nReceived signal " << signal << " (" << signal_name << ")" << std::endl;
    
    if (g_server && g_server->isRunning()) {
        std::cout << "Shutting down server gracefully..." << std::endl;
        g_server->requestShutdown("Signal received");
    } else {
        std::cout << "Force exit..." << std::endl;
        std::exit(signal);
    }
}

void setupSignalHandlers() {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    
#ifndef _WIN32
    std::signal(SIGHUP, signalHandler);
    std::signal(SIGPIPE, SIG_IGN);  // Ignore broken pipe
#endif
}

bool parseCommandLine(int argc, char* argv[], ConfigBuilder& builder) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return false;
        }
        else if (arg == "--version" || arg == "-v") {
            printVersion();
            return false;
        }
        else if (arg == "--port" && i + 1 < argc) {
            builder.withTcpPort(static_cast<uint16_t>(std::stoi(argv[++i])));
        }
        else if (arg == "--bind" && i + 1 < argc) {
            builder.withBindAddress(argv[++i]);
        }
        else if (arg == "--unix-socket" && i + 1 < argc) {
            builder.withUnixSocket(argv[++i]);
        }
        else if (arg == "--max-clients" && i + 1 < argc) {
            builder.withMaxClients(static_cast<uint32_t>(std::stoi(argv[++i])));
        }
        else if (arg == "--no-tcp") {
            builder.enableTcp(false);
        }
        else if (arg == "--no-unix") {
            builder.enableUnixSocket(false);
        }
        else if (arg == "--width" && i + 1 < argc) {
            uint32_t width = static_cast<uint32_t>(std::stoi(argv[++i]));
            if (i + 2 < argc && std::string(argv[i + 1]) == "--height") {
                uint32_t height = static_cast<uint32_t>(std::stoi(argv[i + 2]));
                builder.withWindowSize(width, height);
                i += 2;
            }
        }
        else if (arg == "--fps" && i + 1 < argc) {
            builder.withTargetFPS(static_cast<uint32_t>(std::stoi(argv[++i])));
        }
        else if (arg == "--fullscreen") {
            builder.enableFullscreen(true);
        }
        else if (arg == "--hidden") {
            builder.enableHiddenWindow(true);
        }
        else if (arg == "--no-vsync") {
            builder.enableVSync(false);
        }
        else if (arg == "--no-antialiasing") {
            builder.enableAntialiasing(false);
        }
        else if (arg == "--max-layers" && i + 1 < argc) {
            builder.withMaxLayers(static_cast<uint32_t>(std::stoi(argv[++i])));
        }
        else if (arg == "--no-caching") {
            builder.enableLayerCaching(false);
        }
        else if (arg == "--debug") {
            builder.enableDebugOverlay(true);
            builder.withLogLevel("debug");
        }
        else if (arg == "--log-level" && i + 1 < argc) {
            builder.withLogLevel(argv[++i]);
        }
        else if (arg == "--log-file" && i + 1 < argc) {
            builder.withLogFile(argv[++i]);
        }
        else if (arg == "--config" && i + 1 < argc) {
            // TODO: Load configuration from file
            std::cout << "Loading config from: " << argv[++i] << std::endl;
        }
        else {
            std::cerr << "Unknown option: " << arg << std::endl;
            printUsage(argv[0]);
            return false;
        }
    }
    
    return true;
}

void printStartupInfo(const Server& server) {
    const auto& config = server.getConfig();
    
    std::cout << "\n===============================================================================\n";
    std::cout << "                         SERVER CONFIGURATION                                 \n";
    std::cout << "===============================================================================\n";
    
    std::cout << "Network:\n";
    std::cout << "  TCP Server: " << config.network.tcp_bind_address 
             << ":" << config.network.tcp_port << "\n";
    std::cout << "  Unix Socket: " << config.network.unix_socket_path << "\n";
    std::cout << "  Max Clients: " << config.network.max_clients << "\n";
    
    std::cout << "\nGraphics:\n";
    std::cout << "  Resolution: " << config.renderer.window_width 
             << "x" << config.renderer.window_height << "\n";
    std::cout << "  Target FPS: " << config.renderer.target_fps << "\n";
    std::cout << "  VSync: " << (config.renderer.enable_vsync ? "Enabled" : "Disabled") << "\n";
    std::cout << "  Antialiasing: " << (config.renderer.enable_antialiasing ? "Enabled" : "Disabled") << "\n";
    
    std::cout << "\nFeatures:\n";
    std::cout << "  Max Layers: " << config.features.max_layers << "\n";
    std::cout << "  Layer Caching: " << (config.features.enable_caching ? "Enabled" : "Disabled") << "\n";
    std::cout << "  Command Batching: " << (config.features.enable_batching ? "Enabled" : "Disabled") << "\n";
    std::cout << "  Memory Limit: " << config.performance.max_memory_usage_mb << " MB\n";
    
    std::cout << "\nLogging:\n";
    std::cout << "  Log Level: " << config.logging.log_level << "\n";
    if (config.logging.log_to_file) {
        std::cout << "  Log File: " << config.logging.log_file << "\n";
    }
    std::cout << "===============================================================================\n";
}

void printConnectionInfo(const Server& server) {
    const auto& config = server.getConfig();
    
    std::cout << "\n===============================================================================\n";
    std::cout << "                         CONNECTION INSTRUCTIONS                              \n";
    std::cout << "===============================================================================\n";
    
    if (config.network.enable_tcp) {
        std::cout << "TCP Server listening on: " << config.network.tcp_bind_address 
                 << ":" << config.network.tcp_port << "\n\n";
        
        std::cout << "Connect with TGUI client:\n";
        std::cout << "  auto client = KairosTGUI::Client::create(\"" 
                 << config.network.tcp_bind_address << "\", " 
                 << config.network.tcp_port << ");\n\n";
    }
    
    if (config.network.enable_unix_socket) {
        std::cout << "Unix Socket Server: " << config.network.unix_socket_path << "\n\n";
        
        std::cout << "Connect with Unix socket:\n";
        std::cout << "  auto client = KairosTGUI::Client::createUnix(\"" 
                 << config.network.unix_socket_path << "\");\n\n";
    }
    
    std::cout << "Test connectivity:\n";
    std::cout << "  telnet " << config.network.tcp_bind_address 
             << " " << config.network.tcp_port << "\n\n";
    
    std::cout << "Press Ctrl+C to stop the server gracefully\n";
    std::cout << "===============================================================================\n";
}

int main(int argc, char* argv[]) {
    try {
        printBanner();
        
        // Parse command line arguments
        ConfigBuilder builder;
        if (!parseCommandLine(argc, argv, builder)) {
            return 0; // Help or version was shown
        }
        
        // Build configuration
        auto config = builder.build();
        
        // Initialize logging
        Logger::Config log_config;
        log_config.log_level = (config.logging().log_level == "debug") ? Logger::Level::Debug :
                               (config.logging().log_level == "info") ? Logger::Level::Info :
                               (config.logging().log_level == "warning") ? Logger::Level::Warning :
                               Logger::Level::Error;
        log_config.log_to_console = config.logging().log_to_console;
        log_config.log_to_file = config.logging().log_to_file;
        log_config.log_file = config.logging().log_file;
        
        if (!Logger::initialize(log_config)) {
            std::cerr << "Failed to initialize logging system" << std::endl;
            return 1;
        }
        
        // Create server instance
        {
            std::lock_guard<std::mutex> lock(g_server_mutex);
            g_server = std::make_unique<Server>(config);
        }
        
        if (!g_server) {
            std::cerr << "Failed to create server instance" << std::endl;
            return 1;
        }
        
        // Setup signal handlers
        setupSignalHandlers();
        
        // Initialize server
        std::cout << "Initializing Kairos server..." << std::endl;
        if (!g_server->initialize()) {
            std::cerr << "Failed to initialize server" << std::endl;
            return 1;
        }
        
        // Print configuration
        printStartupInfo(*g_server);
        printConnectionInfo(*g_server);
        
        // Start server
        std::cout << "\nStarting server..." << std::endl;
        Logger::info("Kairos Graphics Server starting up");
        
        g_server->run();
        
        // Server stopped normally
        std::cout << "\nServer stopped." << std::endl;
        Logger::info("Kairos Graphics Server stopped normally");
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "\nFatal error: " << e.what() << std::endl;
        Logger::error("Fatal error: {}", e.what());
        return 1;
    } catch (...) {
        std::cerr << "\nUnknown fatal error occurred" << std::endl;
        Logger::error("Unknown fatal error occurred");
        return 1;
    }
}