// KairosServer/src/Core/NetworkManager.cpp
#include <Core/NetworkManager.hpp>
#include <Network/Client.hpp>
#include <Utils/Logger.hpp>
#include <Graphics/RenderCommand.hpp>
#include <thread>
#include <algorithm>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <sys/un.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <poll.h>
#endif

namespace Kairos {

NetworkManager::NetworkManager(const Config& config) : m_config(config) {
    Logger::info("NetworkManager created");
}

NetworkManager::~NetworkManager() {
    if (m_running) {
        shutdown();
    }
}

bool NetworkManager::initialize() {
    if (m_running) {
        Logger::warning("NetworkManager already initialized");
        return true;
    }
    
    Logger::info("Initializing NetworkManager...");
    
#ifdef _WIN32
    // Initialize Winsock
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        Logger::error("WSAStartup failed: {}", result);
        return false;
    }
#endif
    
    try {
        // Start TCP server if enabled
        if (m_config.enable_tcp && !startTcpServer()) {
            Logger::error("Failed to start TCP server");
            return false;
        }
        
        // Start Unix socket server if enabled
        if (m_config.enable_unix_socket && !startUnixSocketServer()) {
            Logger::error("Failed to start Unix socket server");
            return false;
        }
        
        // Start network threads
        m_running = true;
        m_accepting_connections = true;
        
        for (uint32_t i = 0; i < m_config.network_thread_count; ++i) {
            m_network_threads.emplace_back(&NetworkManager::networkThreadMain, this);
        }
        
        m_accept_thread = std::thread(&NetworkManager::acceptConnections, this);
        
        Logger::info("NetworkManager initialized successfully");
        return true;
        
    } catch (const std::exception& e) {
        Logger::error("Exception during NetworkManager initialization: {}", e.what());
        shutdown();
        return false;
    }
}

void NetworkManager::shutdown() {
    if (!m_running) {
        return;
    }
    
    Logger::info("Shutting down NetworkManager...");
    
    m_running = false;
    m_accepting_connections = false;
    
    // Stop accepting new connections
    if (m_accept_thread.joinable()) {
        m_accept_thread.join();
    }
    
    // Disconnect all clients
    {
        std::lock_guard<std::mutex> lock(m_clients_mutex);
        for (auto& [client_id, client] : m_clients) {
            client->disconnect("Server shutdown");
        }
        m_clients.clear();
    }
    
    // Stop network threads
    for (auto& thread : m_network_threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    m_network_threads.clear();
    
    // Close server sockets
    stopAllServers();
    
#ifdef _WIN32
    WSACleanup();
#endif
    
    Logger::info("NetworkManager shutdown complete");
}

bool NetworkManager::startTcpServer() {
    Logger::info("Starting TCP server on {}:{}", m_config.tcp_bind_address, m_config.tcp_port);
    
    if (!createTcpSocket()) {
        return false;
    }
    
    // Bind socket
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(m_config.tcp_port);
    
    if (inet_pton(AF_INET, m_config.tcp_bind_address.c_str(), &addr.sin_addr) <= 0) {
        Logger::error("Invalid bind address: {}", m_config.tcp_bind_address);
        return false;
    }
    
    if (bind(m_tcp_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        Logger::error("Failed to bind TCP socket: {}", strerror(errno));
        return false;
    }
    
    // Listen for connections
    if (listen(m_tcp_socket, 32) < 0) {
        Logger::error("Failed to listen on TCP socket: {}", strerror(errno));
        return false;
    }
    
    Logger::info("TCP server started successfully");
    return true;
}

bool NetworkManager::startUnixSocketServer() {
    Logger::info("Starting Unix socket server: {}", m_config.unix_socket_path);
    
#ifdef _WIN32
    Logger::warning("Unix sockets not supported on Windows");
    return false;
#else
    
    if (!createUnixSocket()) {
        return false;
    }
    
    // Remove existing socket file
    unlink(m_config.unix_socket_path.c_str());
    
    // Bind socket
    struct sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, m_config.unix_socket_path.c_str(), sizeof(addr.sun_path) - 1);
    
    if (bind(m_unix_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        Logger::error("Failed to bind Unix socket: {}", strerror(errno));
        return false;
    }
    
    // Listen for connections
    if (listen(m_unix_socket, 32) < 0) {
        Logger::error("Failed to listen on Unix socket: {}", strerror(errno));
        return false;
    }
    
    Logger::info("Unix socket server started successfully");
    return true;
    
#endif
}

void NetworkManager::stopAllServers() {
    if (m_tcp_socket != -1) {
#ifdef _WIN32
        closesocket(m_tcp_socket);
#else
        close(m_tcp_socket);
#endif
        m_tcp_socket = -1;
    }
    
    if (m_unix_socket != -1) {
#ifndef _WIN32
        close(m_unix_socket);
        unlink(m_config.unix_socket_path.c_str());
#endif
        m_unix_socket = -1;
    }
}

std::vector<uint32_t> NetworkManager::getConnectedClients() const {
    std::lock_guard<std::mutex> lock(m_clients_mutex);
    
    std::vector<uint32_t> client_ids;
    for (const auto& [client_id, client] : m_clients) {
        if (client->isConnected()) {
            client_ids.push_back(client_id);
        }
    }
    
    return client_ids;
}

bool NetworkManager::disconnectClient(uint32_t client_id, const std::string& reason) {
    std::lock_guard<std::mutex> lock(m_clients_mutex);
    
    auto it = m_clients.find(client_id);
    if (it != m_clients.end()) {
        it->second->disconnect(reason);
        Logger::info("Disconnected client {} ({})", client_id, reason);
        return true;
    }
    
    return false;
}

std::shared_ptr<Client> NetworkManager::getClient(uint32_t client_id) const {
    std::lock_guard<std::mutex> lock(m_clients_mutex);
    
    auto it = m_clients.find(client_id);
    return (it != m_clients.end()) ? it->second : nullptr;
}

bool NetworkManager::sendMessage(uint32_t client_id, const MessageHeader& header, const void* data) {
    auto client = getClient(client_id);
    if (!client || !client->isConnected()) {
        return false;
    }
    
    bool success = client->sendMessage(header, data);
    if (success) {
        m_stats.messages_sent.fetch_add(1);
        m_stats.bytes_sent.fetch_add(sizeof(MessageHeader) + header.data_size);
    }
    
    return success;
}

bool NetworkManager::broadcastMessage(const MessageHeader& header, const void* data) {
    std::lock_guard<std::mutex> lock(m_clients_mutex);
    
    size_t sent_count = 0;
    for (auto& [client_id, client] : m_clients) {
        if (client->isConnected() && client->sendMessage(header, data)) {
            sent_count++;
        }
    }
    
    if (sent_count > 0) {
        m_stats.messages_sent.fetch_add(sent_count);
        m_stats.bytes_sent.fetch_add(sent_count * (sizeof(MessageHeader) + header.data_size));
    }
    
    return sent_count > 0;
}

bool NetworkManager::sendToLayer(uint8_t layer_id, const MessageHeader& header, const void* data) {
    std::lock_guard<std::mutex> lock(m_clients_mutex);
    
    size_t sent_count = 0;
    for (auto& [client_id, client] : m_clients) {
        // In a real implementation, clients would have layer subscriptions
        // For now, send to all connected clients
        if (client->isConnected() && client->sendMessage(header, data)) {
            sent_count++;
        }
    }
    
    return sent_count > 0;
}

bool NetworkManager::sendInputEvent(uint32_t client_id, const InputEvent& event) {
    MessageHeader header = ProtocolHelper::createHeader(MessageType::INPUT_EVENT, client_id, 0, sizeof(InputEvent));
    return sendMessage(client_id, header, &event);
}

bool NetworkManager::sendFrameCallback(uint32_t client_id, const FrameCallback& callback) {
    MessageHeader header = ProtocolHelper::createHeader(MessageType::FRAME_CALLBACK, client_id, 0, sizeof(FrameCallback));
    return sendMessage(client_id, header, &callback);
}

bool NetworkManager::sendErrorResponse(uint32_t client_id, ErrorCode error_code, 
                                      const std::string& message, uint32_t original_sequence) {
    ErrorResponse response = ProtocolHelper::createErrorResponse(error_code, message, original_sequence);
    MessageHeader header = ProtocolHelper::createHeader(MessageType::ERROR_RESPONSE, client_id, 0, sizeof(ErrorResponse));
    return sendMessage(client_id, header, &response);
}

bool NetworkManager::sendPing(uint32_t client_id) {
    auto client = getClient(client_id);
    if (!client) {
        return false;
    }
    
    client->sendPing();
    return true;
}

void NetworkManager::handlePong(uint32_t client_id, const PongData& pong_data) {
    auto client = getClient(client_id);
    if (client) {
        client->handlePong(pong_data);
    }
}

void NetworkManager::setConfig(const Config& config) {
    if (m_running) {
        Logger::warning("Cannot change NetworkManager configuration while running");
        return;
    }
    
    m_config = config;
    Logger::info("NetworkManager configuration updated");
}

const NetworkManager::Stats& NetworkManager::getStats() const {
    return m_stats;
}

void NetworkManager::resetStats() {
    m_stats = Stats{};
    Logger::debug("NetworkManager statistics reset");
}

void NetworkManager::setClientConnectedCallback(ClientConnectedCallback callback) {
    m_client_connected_callback = callback;
}

void NetworkManager::setClientDisconnectedCallback(ClientDisconnectedCallback callback) {
    m_client_disconnected_callback = callback;
}

void NetworkManager::setCommandReceivedCallback(CommandReceivedCallback callback) {
    m_command_received_callback = callback;
}

void NetworkManager::setErrorCallback(ErrorCallback callback) {
    m_error_callback = callback;
}

// Private methods implementation

void NetworkManager::networkThreadMain() {
    Logger::debug("Network thread started");
    
    while (m_running) {
        try {
            // Process client messages
            std::vector<std::shared_ptr<Client>> clients_to_process;
            
            {
                std::lock_guard<std::mutex> lock(m_clients_mutex);
                for (auto& [client_id, client] : m_clients) {
                    if (client->isConnected()) {
                        clients_to_process.push_back(client);
                    }
                }
            }
            
            for (auto& client : clients_to_process) {
                processClientMessages(client);
            }
            
            // Cleanup disconnected clients
            cleanupDisconnectedClients();
            
            // Update rate limits
            updateRateLimits();
            
            // Sleep briefly to avoid busy waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            
        } catch (const std::exception& e) {
            Logger::error("Exception in network thread: {}", e.what());
        }
    }
    
    Logger::debug("Network thread stopped");
}

void NetworkManager::acceptConnections() {
    Logger::debug("Accept thread started");
    
    while (m_accepting_connections) {
        try {
            // Use poll/select to wait for incoming connections
            fd_set read_fds;
            FD_ZERO(&read_fds);
            
            int max_fd = -1;
            
            if (m_tcp_socket != -1) {
                FD_SET(m_tcp_socket, &read_fds);
                max_fd = std::max(max_fd, m_tcp_socket);
            }
            
            if (m_unix_socket != -1) {
                FD_SET(m_unix_socket, &read_fds);
                max_fd = std::max(max_fd, m_unix_socket);
            }
            
            if (max_fd == -1) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            
            struct timeval timeout = {0, 100000}; // 100ms timeout
            int result = select(max_fd + 1, &read_fds, nullptr, nullptr, &timeout);
            
            if (result < 0) {
                if (errno != EINTR) {
                    Logger::error("Select failed: {}", strerror(errno));
                }
                continue;
            }
            
            if (result == 0) {
                continue; // Timeout
            }
            
            // Check for TCP connections
            if (m_tcp_socket != -1 && FD_ISSET(m_tcp_socket, &read_fds)) {
                auto client = acceptTcpConnection();
                if (client) {
                    handleNewClient(client);
                }
            }
            
            // Check for Unix socket connections
            if (m_unix_socket != -1 && FD_ISSET(m_unix_socket, &read_fds)) {
                auto client = acceptUnixConnection();
                if (client) {
                    handleNewClient(client);
                }
            }
            
        } catch (const std::exception& e) {
            Logger::error("Exception in accept thread: {}", e.what());
        }
    }
    
    Logger::debug("Accept thread stopped");
}

std::shared_ptr<Client> NetworkManager::acceptTcpConnection() {
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    
    int client_socket = accept(m_tcp_socket, (struct sockaddr*)&client_addr, &addr_len);
    if (client_socket < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            Logger::error("Failed to accept TCP connection: {}", strerror(errno));
        }
        return nullptr;
    }
    
    // Get client address
    char addr_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, addr_str, INET_ADDRSTRLEN);
    uint16_t port = ntohs(client_addr.sin_port);
    
    Logger::info("Accepted TCP connection from {}:{}", addr_str, port);
    
    return Client::createTcp(client_socket, addr_str, port);
}

std::shared_ptr<Client> NetworkManager::acceptUnixConnection() {
#ifdef _WIN32
    return nullptr;
#else
    struct sockaddr_un client_addr;
    socklen_t addr_len = sizeof(client_addr);
    
    int client_socket = accept(m_unix_socket, (struct sockaddr*)&client_addr, &addr_len);
    if (client_socket < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            Logger::error("Failed to accept Unix socket connection: {}", strerror(errno));
        }
        return nullptr;
    }
    
    Logger::info("Accepted Unix socket connection");
    
    return Client::createUnix(client_socket, m_config.unix_socket_path);
#endif
}

void NetworkManager::handleNewClient(std::shared_ptr<Client> client) {
    // Check connection limits
    {
        std::lock_guard<std::mutex> lock(m_clients_mutex);
        if (m_clients.size() >= m_config.max_clients) {
            Logger::warning("Connection limit reached, rejecting client");
            client->disconnect("Server full");
            return;
        }
    }
    
    // Initialize client
    uint32_t client_id = generateClientId();
    Client::Config client_config;
    client_config.receive_buffer_size = m_config.receive_buffer_size;
    client_config.send_buffer_size = m_config.send_buffer_size;
    client_config.timeout_seconds = m_config.client_timeout_seconds;
    
    if (!client->initialize(client_id, client_config)) {
        Logger::error("Failed to initialize client {}", client_id);
        return;
    }
    
    // Perform handshake
    if (!handleClientHandshake(client)) {
        Logger::error("Handshake failed for client {}", client_id);
        return;
    }
    
    // Add to client list
    {
        std::lock_guard<std::mutex> lock(m_clients_mutex);
        m_clients[client_id] = client;
    }
    
    m_stats.total_connections.fetch_add(1);
    m_stats.active_connections.fetch_add(1);
    
    // Notify callback
    if (m_client_connected_callback) {
        const auto& info = client->getInfo();
        m_client_connected_callback(client_id, info.client_name);
    }
    
    Logger::info("Client {} connected successfully", client_id);
}

bool NetworkManager::handleClientHandshake(std::shared_ptr<Client> client) {
    // Wait for CLIENT_HELLO message
    std::vector<std::pair<MessageHeader, std::vector<uint8_t>>> messages;
    
    auto timeout_start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - timeout_start < std::chrono::seconds(m_config.handshake_timeout_seconds)) {
        if (client->receiveMessages(messages) && !messages.empty()) {
            for (const auto& [header, data] : messages) {
                if (header.type == MessageType::CLIENT_HELLO && data.size() == sizeof(ClientHello)) {
                    ClientHello hello;
                    std::memcpy(&hello, data.data(), sizeof(ClientHello));
                    
                    handleClientHello(client, hello);
                    return true;
                }
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    Logger::warning("Handshake timeout for client");
    return false;
}

void NetworkManager::handleClientHello(std::shared_ptr<Client> client, const ClientHello& hello) {
    Logger::info("Received CLIENT_HELLO from {}", hello.client_name);
    
    // Create SERVER_HELLO response
    ServerHello server_hello = ProtocolHelper::createServerHello(client->getId(), PROTOCOL_VERSION);
    
    MessageHeader header = ProtocolHelper::createHeader(MessageType::SERVER_HELLO, client->getId(), 0, sizeof(ServerHello));
    
    if (!client->sendMessage(header, &server_hello)) {
        Logger::error("Failed to send SERVER_HELLO to client {}", client->getId());
        return;
    }
    
    // Perform handshake
    client->performHandshake(server_hello);
    
    Logger::info("Handshake completed for client {}", client->getId());
}

void NetworkManager::processClientMessages(std::shared_ptr<Client> client) {
    if (!client->isConnected()) {
        return;
    }
    
    // Check for timeout
    if (client->isTimedOut()) {
        Logger::info("Client {} timed out", client->getId());
        client->disconnect("Timeout");
        return;
    }
    
    // Send ping if needed
    if (client->needsPing()) {
        client->sendPing();
    }
    
    // Receive and process messages
    std::vector<std::pair<MessageHeader, std::vector<uint8_t>>> messages;
    if (client->receiveMessages(messages)) {
        for (auto& [header, data] : messages) {
            if (!processMessage(client, header, data)) {
                Logger::warning("Failed to process message from client {}", client->getId());
                m_stats.invalid_messages.fetch_add(1);
            }
        }
        
        m_stats.messages_received.fetch_add(messages.size());
    }
}

bool NetworkManager::processMessage(std::shared_ptr<Client> client, const MessageHeader& header, 
                                   const std::vector<uint8_t>& data) {
    // Validate message
    if (!validateMessage(header, data)) {
        return false;
    }
    
    // Update client activity
    client->updateActivity();
    
    // Handle specific message types
    switch (header.type) {
        case MessageType::PING: {
            if (data.size() == sizeof(PingData)) {
                PingData ping_data;
                std::memcpy(&ping_data, data.data(), sizeof(PingData));
                handlePing(client, ping_data);
            }
            break;
        }
        
        case MessageType::PONG: {
            if (data.size() == sizeof(PongData)) {
                PongData pong_data;
                std::memcpy(&pong_data, data.data(), sizeof(PongData));
                client->handlePong(pong_data);
            }
            break;
        }
        
        case MessageType::DISCONNECT: {
            handleDisconnect(client);
            break;
        }
        
        default: {
            // Convert to render command and forward to callback
            if (m_command_received_callback) {
                RenderCommand command = CommandConverter::fromNetworkMessage(header, data.data());
                m_command_received_callback(client->getId(), std::move(command));
            }
            break;
        }
    }
    
    m_stats.bytes_received.fetch_add(sizeof(MessageHeader) + data.size());
    return true;
}

void NetworkManager::handlePing(std::shared_ptr<Client> client, const PingData& ping) {
    // Create PONG response
    PongData pong = ProtocolHelper::createPongResponse(ping, 0, 0); // TODO: Get actual server load and queue depth
    
    MessageHeader header = ProtocolHelper::createHeader(MessageType::PONG, client->getId(), 0, sizeof(PongData));
    
    client->sendMessage(header, &pong);
}

void NetworkManager::handleDisconnect(std::shared_ptr<Client> client) {
    Logger::info("Client {} requested disconnect", client->getId());
    client->disconnect("Client request");
}

void NetworkManager::cleanupDisconnectedClients() {
    std::lock_guard<std::mutex> lock(m_clients_mutex);
    
    auto it = m_clients.begin();
    while (it != m_clients.end()) {
        if (!it->second->isConnected()) {
            uint32_t client_id = it->first;
            
            // Notify callback
            if (m_client_disconnected_callback) {
                m_client_disconnected_callback(client_id, "Disconnected");
            }
            
            it = m_clients.erase(it);
            m_stats.active_connections.fetch_sub(1);
            
            Logger::debug("Cleaned up disconnected client {}", client_id);
        } else {
            ++it;
        }
    }
}

bool NetworkManager::createTcpSocket() {
    m_tcp_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (m_tcp_socket < 0) {
        Logger::error("Failed to create TCP socket: {}", strerror(errno));
        return false;
    }
    
    // Set socket options
    int opt = 1;
    if (setsockopt(m_tcp_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        Logger::warning("Failed to set SO_REUSEADDR: {}", strerror(errno));
    }
    
    if (m_config.enable_tcp_nodelay) {
        if (setsockopt(m_tcp_socket, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) < 0) {
            Logger::warning("Failed to set TCP_NODELAY: {}", strerror(errno));
        }
    }
    
    return true;
}

bool NetworkManager::createUnixSocket() {
#ifdef _WIN32
    return false;
#else
    m_unix_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (m_unix_socket < 0) {
        Logger::error("Failed to create Unix socket: {}", strerror(errno));
        return false;
    }
    
    return true;
#endif
}

void NetworkManager::updateRateLimits() {
    if (!m_config.enable_rate_limiting) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(m_rate_limit_mutex);
    auto now = std::chrono::steady_clock::now();
    
    for (auto& [client_id, rate_info] : m_rate_limits) {
        // Reset rate limit window every second
        if (now - rate_info.last_reset >= std::chrono::seconds(1)) {
            rate_info.command_count = 0;
            rate_info.last_reset = now;
            rate_info.is_limited = false;
        }
    }
}

bool NetworkManager::checkRateLimit(uint32_t client_id) {
    if (!m_config.enable_rate_limiting) {
        return true;
    }
    
    std::lock_guard<std::mutex> lock(m_rate_limit_mutex);
    auto& rate_info = m_rate_limits[client_id];
    
    rate_info.command_count++;
    
    if (rate_info.command_count > m_config.max_commands_per_second) {
        if (!rate_info.is_limited) {
            Logger::warning("Rate limit exceeded for client {}", client_id);
            rate_info.is_limited = true;
        }
        return false;
    }
    
    return true;
}

bool NetworkManager::validateMessage(const MessageHeader& header, const std::vector<uint8_t>& data) {
    if (!ProtocolHelper::validateHeader(header)) {
        return false;
    }
    
    if (data.size() != header.data_size) {
        return false;
    }
    
    if (header.data_size > m_config.max_message_size) {
        return false;
    }
    
    return true;
}

bool NetworkManager::validateClient(uint32_t client_id) const {
    std::lock_guard<std::mutex> lock(m_clients_mutex);
    return m_clients.find(client_id) != m_clients.end();
}

uint32_t NetworkManager::generateClientId() {
    return m_next_client_id.fetch_add(1);
}

std::string NetworkManager::getClientEndpointInfo(std::shared_ptr<Client> client) const {
    const auto& info = client->getInfo();
    if (info.connection_type == Client::Type::TCP) {
        return info.endpoint_address + ":" + std::to_string(info.endpoint_port);
    } else {
        return info.endpoint_address;
    }
}

void NetworkManager::updateStats() {
    std::lock_guard<std::mutex> lock(m_stats_mutex);
    
    auto now = std::chrono::steady_clock::now();
    if (now - m_last_stats_update >= std::chrono::seconds(1)) {
        // Update periodic statistics here
        m_last_stats_update = now;
    }
}

void NetworkManager::logConnectionEvent(const std::string& event, uint32_t client_id, const std::string& details) {
    if (details.empty()) {
        Logger::info("Connection event - {}: Client {}", event, client_id);
    } else {
        Logger::info("Connection event - {}: Client {} ({})", event, client_id, details);
    }
}

} // namespace Kairos