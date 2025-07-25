// KairosServer/src/Network/SocketManager.cpp
#include <Network/SocketManager.hpp>
#include <Utils/Logger.hpp>
#include <algorithm>
#include <chrono>

namespace Kairos {

SocketManager::SocketManager(const Config& config) : m_config(config) {
    Logger::info("SocketManager created");
}

SocketManager::~SocketManager() {
    if (m_running) {
        shutdown();
    }
}

bool SocketManager::initialize() {
    if (m_running) {
        Logger::warning("SocketManager already initialized");
        return true;
    }
    
    Logger::info("Initializing SocketManager...");
    
    try {
        // Initialize network subsystem
        if (m_config.enable_tcp && !TCPUtils::initializeNetworking()) {
            Logger::error("Failed to initialize TCP networking");
            return false;
        }
        
        m_running = true;
        m_last_cleanup = std::chrono::steady_clock::now();
        
        // Start event processing thread
        m_event_thread = std::thread(&SocketManager::eventLoop, this);
        
        Logger::info("SocketManager initialized successfully");
        return true;
        
    } catch (const std::exception& e) {
        Logger::error("Exception during SocketManager initialization: {}", e.what());
        return false;
    }
}

void SocketManager::shutdown() {
    if (!m_running) {
        return;
    }
    
    Logger::info("Shutting down SocketManager...");
    
    m_running = false;
    
    // Stop event thread
    if (m_event_thread.joinable()) {
        m_event_thread.join();
    }
    
    // Close all sockets
    closeAll();
    
    // Cleanup network subsystem
    if (m_config.enable_tcp) {
        TCPUtils::cleanupNetworking();
    }
    
    Logger::info("SocketManager shutdown complete");
}

uint32_t SocketManager::createTcpSocket() {
    if (!m_config.enable_tcp) {
        Logger::error("TCP sockets are disabled");
        return 0;
    }
    
    std::unique_lock<std::shared_mutex> lock(m_sockets_mutex);
    
    if (m_sockets.size() >= m_config.max_sockets) {
        Logger::error("Maximum socket limit reached");
        return 0;
    }
    
    uint32_t socket_id = generateSocketId();
    
    try {
        auto managed_socket = std::make_shared<ManagedSocket>(socket_id, SocketType::TCP);
        managed_socket->tcp_socket = std::make_unique<TCPSocket>();
        
        managed_socket->info.socket_id = socket_id;
        managed_socket->info.type = SocketType::TCP;
        managed_socket->info.created_time = std::chrono::steady_clock::now();
        managed_socket->info.last_activity = managed_socket->info.created_time;
        
        addSocket(managed_socket);
        
        m_stats.total_sockets_created++;
        m_stats.active_tcp_sockets++;
        
        Logger::debug("Created TCP socket {}", socket_id);
        return socket_id;
        
    } catch (const std::exception& e) {
        Logger::error("Failed to create TCP socket: {}", e.what());
        return 0;
    }
}

bool SocketManager::connectTcp(uint32_t socket_id, const std::string& address, uint16_t port) {
    auto socket = getSocket(socket_id);
    if (!socket || socket->type != SocketType::TCP || !socket->tcp_socket) {
        Logger::error("Invalid TCP socket ID: {}", socket_id);
        return false;
    }
    
    std::lock_guard<std::mutex> lock(socket->socket_mutex);
    
    if (socket->tcp_socket->connect(address, port)) {
        socket->info.address = address;
        socket->info.port = port;
        socket->info.is_connected = true;
        socket->updateActivity();
        
        m_stats.total_connections++;
        
        if (m_connected_callback) {
            m_connected_callback(socket_id, socket->info);
        }
        
        Logger::info("Connected TCP socket {} to {}:{}", socket_id, address, port);
        return true;
    }
    
    m_stats.failed_connections++;
    Logger::error("Failed to connect TCP socket {} to {}:{}", socket_id, address, port);
    return false;
}

uint32_t SocketManager::createTcpServer(const std::string& bind_address, uint16_t port) {
    if (!m_config.enable_tcp) {
        Logger::error("TCP sockets are disabled");
        return 0;
    }
    
    std::unique_lock<std::shared_mutex> lock(m_sockets_mutex);
    
    uint32_t socket_id = generateSocketId();
    
    try {
        auto managed_socket = std::make_shared<ManagedSocket>(socket_id, SocketType::TCP);
        managed_socket->tcp_socket = std::make_unique<TCPSocket>();
        
        if (!managed_socket->tcp_socket->bind(bind_address, port) ||
            !managed_socket->tcp_socket->listen()) {
            Logger::error("Failed to create TCP server on {}:{}", bind_address, port);
            return 0;
        }
        
        managed_socket->info.socket_id = socket_id;
        managed_socket->info.type = SocketType::TCP;
        managed_socket->info.address = bind_address;
        managed_socket->info.port = port;
        managed_socket->info.is_listening = true;
        managed_socket->info.created_time = std::chrono::steady_clock::now();
        managed_socket->info.last_activity = managed_socket->info.created_time;
        
        // Set non-blocking for polling
        managed_socket->tcp_socket->setNonBlocking(true);
        
        addSocket(managed_socket);
        m_listening_sockets.push_back(socket_id);
        
        m_stats.total_sockets_created++;
        m_stats.active_tcp_sockets++;
        m_stats.listening_sockets++;
        
        Logger::info("Created TCP server {} on {}:{}", socket_id, bind_address, port);
        return socket_id;
        
    } catch (const std::exception& e) {
        Logger::error("Failed to create TCP server: {}", e.what());
        return 0;
    }
}

uint32_t SocketManager::createUnixSocket() {
    if (!m_config.enable_unix_sockets) {
        Logger::error("Unix sockets are disabled");
        return 0;
    }
    
    if (!UnixSocket::isSupported()) {
        Logger::error("Unix sockets not supported on this platform");
        return 0;
    }
    
    std::unique_lock<std::shared_mutex> lock(m_sockets_mutex);
    
    if (m_sockets.size() >= m_config.max_sockets) {
        Logger::error("Maximum socket limit reached");
        return 0;
    }
    
    uint32_t socket_id = generateSocketId();
    
    try {
        auto managed_socket = std::make_shared<ManagedSocket>(socket_id, SocketType::UNIX_SOCKET);
        managed_socket->unix_socket = std::make_unique<UnixSocket>();
        
        managed_socket->info.socket_id = socket_id;
        managed_socket->info.type = SocketType::UNIX_SOCKET;
        managed_socket->info.created_time = std::chrono::steady_clock::now();
        managed_socket->info.last_activity = managed_socket->info.created_time;
        
        addSocket(managed_socket);
        
        m_stats.total_sockets_created++;
        m_stats.active_unix_sockets++;
        
        Logger::debug("Created Unix socket {}", socket_id);
        return socket_id;
        
    } catch (const std::exception& e) {
        Logger::error("Failed to create Unix socket: {}", e.what());
        return 0;
    }
}

bool SocketManager::connectUnix(uint32_t socket_id, const std::string& socket_path) {
    auto socket = getSocket(socket_id);
    if (!socket || socket->type != SocketType::UNIX_SOCKET || !socket->unix_socket) {
        Logger::error("Invalid Unix socket ID: {}", socket_id);
        return false;
    }
    
    std::lock_guard<std::mutex> lock(socket->socket_mutex);
    
    if (socket->unix_socket->connect(socket_path)) {
        socket->info.address = socket_path;
        socket->info.is_connected = true;
        socket->updateActivity();
        
        m_stats.total_connections++;
        
        if (m_connected_callback) {
            m_connected_callback(socket_id, socket->info);
        }
        
        Logger::info("Connected Unix socket {} to {}", socket_id, socket_path);
        return true;
    }
    
    m_stats.failed_connections++;
    Logger::error("Failed to connect Unix socket {} to {}", socket_id, socket_path);
    return false;
}

uint32_t SocketManager::createUnixServer(const std::string& socket_path) {
    if (!m_config.enable_unix_sockets) {
        Logger::error("Unix sockets are disabled");
        return 0;
    }
    
    if (!UnixSocket::isSupported()) {
        Logger::error("Unix sockets not supported on this platform");
        return 0;
    }
    
    std::unique_lock<std::shared_mutex> lock(m_sockets_mutex);
    
    uint32_t socket_id = generateSocketId();
    
    try {
        auto managed_socket = std::make_shared<ManagedSocket>(socket_id, SocketType::UNIX_SOCKET);
        managed_socket->unix_socket = std::make_unique<UnixSocket>();
        
        if (!managed_socket->unix_socket->bind(socket_path) ||
            !managed_socket->unix_socket->listen()) {
            Logger::error("Failed to create Unix server on {}", socket_path);
            return 0;
        }
        
        managed_socket->info.socket_id = socket_id;
        managed_socket->info.type = SocketType::UNIX_SOCKET;
        managed_socket->info.address = socket_path;
        managed_socket->info.is_listening = true;
        managed_socket->info.created_time = std::chrono::steady_clock::now();
        managed_socket->info.last_activity = managed_socket->info.created_time;
        
        // Set non-blocking for polling
        managed_socket->unix_socket->setNonBlocking(true);
        
        addSocket(managed_socket);
        m_listening_sockets.push_back(socket_id);
        
        m_stats.total_sockets_created++;
        m_stats.active_unix_sockets++;
        m_stats.listening_sockets++;
        
        Logger::info("Created Unix server {} on {}", socket_id, socket_path);
        return socket_id;
        
    } catch (const std::exception& e) {
        Logger::error("Failed to create Unix server: {}", e.what());
        return 0;
    }
}

bool SocketManager::closeSocket(uint32_t socket_id) {
    std::unique_lock<std::shared_mutex> lock(m_sockets_mutex);
    
    auto it = m_sockets.find(socket_id);
    if (it == m_sockets.end()) {
        Logger::warning("Attempted to close non-existent socket {}", socket_id);
        return false;
    }
    
    auto socket = it->second;
    std::string reason = "Socket closed by request";
    
    // Notify disconnection if connected
    if (socket->info.is_connected && m_disconnected_callback) {
        m_disconnected_callback(socket_id, reason);
    }
    
    // Update statistics
    if (socket->type == SocketType::TCP) {
        m_stats.active_tcp_sockets--;
    } else {
        m_stats.active_unix_sockets--;
    }
    
    if (socket->info.is_listening) {
        m_stats.listening_sockets--;
        // Remove from listening sockets list
        auto listen_it = std::find(m_listening_sockets.begin(), m_listening_sockets.end(), socket_id);
        if (listen_it