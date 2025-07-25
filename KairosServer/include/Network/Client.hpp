// KairosServer/src/Network/Client.hpp
#pragma once

#include "KairosShared/Protocol.hpp"
#include <memory>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <chrono>
#include <queue>

#ifdef _WIN32
    #include <winsock2.h>
    using socket_t = SOCKET;
#else
    #include <sys/socket.h>
    using socket_t = int;
#endif

namespace Kairos {

/**
 * @brief Represents a connected client (TCP or Unix socket)
 * 
 * Handles:
 * - Socket communication
 * - Message buffering and parsing
 * - Client state management
 * - Rate limiting
 * - Keep-alive/ping-pong
 */
class Client {
public:
    enum class Type {
        TCP,
        UNIX_SOCKET
    };
    
    enum class State {
        CONNECTING,
        HANDSHAKE,
        CONNECTED,
        DISCONNECTING,
        DISCONNECTED,
        ERROR
    };
    
    struct Info {
        uint32_t client_id = 0;
        std::string client_name;
        uint32_t client_version = 0;
        uint32_t capabilities = 0;
        uint32_t requested_layers = 1;
        
        Type connection_type = Type::TCP;
        std::string endpoint_address;
        uint16_t endpoint_port = 0;
        
        std::chrono::steady_clock::time_point connect_time;
        std::chrono::steady_clock::time_point last_activity;
        
        // Statistics
        uint64_t messages_sent = 0;
        uint64_t messages_received = 0;
        uint64_t bytes_sent = 0;
        uint64_t bytes_received = 0;
        uint32_t errors = 0;
        
        double avg_latency_ms = 0.0;
        uint32_t ping_sequence = 0;
        std::chrono::steady_clock::time_point last_ping_time;
    };
    
    struct Config {
        size_t receive_buffer_size = 64 * 1024;     // 64KB
        size_t send_buffer_size = 64 * 1024;        // 64KB
        uint32_t timeout_seconds = 30;
        uint32_t ping_interval_seconds = 10;
        uint32_t max_message_size = 10 * 1024 * 1024; // 10MB
        bool enable_keep_alive = true;
        bool enable_nagle = false;  // TCP_NODELAY for low latency
    };

public:
    // Factory methods
    static std::shared_ptr<Client> createTcp(socket_t socket, const std::string& address, uint16_t port);
    static std::shared_ptr<Client> createUnix(socket_t socket, const std::string& path);
    
    ~Client();
    
    // Basic properties
    uint32_t getId() const { return m_info.client_id; }
    Type getType() const { return m_info.connection_type; }
    State getState() const { return m_state.load(); }
    const Info& getInfo() const { return m_info; }
    
    // Connection management
    bool initialize(uint32_t client_id, const Config& config = Config{});
    void disconnect(const std::string& reason = "");
    bool isConnected() const;
    bool isTimedOut() const;
    
    // Message handling
    bool sendMessage(const MessageHeader& header, const void* data = nullptr);
    bool receiveMessages(std::vector<std::pair<MessageHeader, std::vector<uint8_t>>>& messages);
    
    // Handshake
    bool performHandshake(const ServerHello& server_hello);
    bool isHandshakeComplete() const { return m_state.load() == State::CONNECTED; }
    
    // Keep-alive
    void sendPing();
    void handlePong(const PongData& pong);
    bool needsPing() const;
    
    // Rate limiting and validation
    bool checkRateLimit();
    void updateActivity();
    
    // Statistics
    void updateLatency(double latency_ms);
    std::string getStatusString() const;
    
    // Configuration
    void setConfig(const Config& config) { m_config = config; }
    const Config& getConfig() const { return m_config; }

private:
    explicit Client(socket_t socket, Type type);
    
    // Internal message handling
    bool sendRawData(const void* data, size_t size);
    bool receiveRawData();
    bool parseMessages();
    
    // Buffer management
    bool ensureReceiveBufferSpace(size_t needed_space);
    bool ensureSendBufferSpace(size_t needed_space);
    void compactReceiveBuffer();
    
    // Socket configuration
    bool configureSocket();
    bool setSocketNonBlocking();
    bool setSocketOptions();
    
    // Validation
    bool validateMessageHeader(const MessageHeader& header) const;
    bool validateMessageSize(size_t size) const;
    
    // Utilities
    void setState(State new_state);
    void logError(const std::string& message);
    std::string getEndpointString() const;

private:
    socket_t m_socket;
    Config m_config;
    Info m_info;
    std::atomic<State> m_state{State::CONNECTING};
    
    // Buffers
    std::vector<uint8_t> m_receive_buffer;
    std::vector<uint8_t> m_send_buffer;
    size_t m_receive_buffer_pos = 0;
    size_t m_send_buffer_pos = 0;
    
    // Parsed messages waiting to be processed
    std::queue<std::pair<MessageHeader, std::vector<uint8_t>>> m_parsed_messages;
    
    // Thread safety
    mutable std::mutex m_send_mutex;
    mutable std::mutex m_receive_mutex;
    mutable std::mutex m_info_mutex;
    
    // Rate limiting
    std::queue<std::chrono::steady_clock::time_point> m_message_times;
    static constexpr size_t MAX_MESSAGES_PER_SECOND = 1000;
    
    // Keep-alive tracking
    std::chrono::steady_clock::time_point m_last_ping_sent;
    std::chrono::steady_clock::time_point m_last_pong_received;
    
    // Error tracking
    uint32_t m_consecutive_errors = 0;
    static constexpr uint32_t MAX_CONSECUTIVE_ERRORS = 5;
    
    // Disconnect reason
    std::string m_disconnect_reason;
};

/**
 * @brief Client manager for handling multiple clients efficiently
 */
class ClientManager {
public:
    explicit ClientManager(size_t max_clients = 1000);
    ~ClientManager();
    
    // Client lifecycle
    bool addClient(std::shared_ptr<Client> client);
    bool removeClient(uint32_t client_id);
    std::shared_ptr<Client> getClient(uint32_t client_id) const;
    
    // Bulk operations
    std::vector<std::shared_ptr<Client>> getAllClients() const;
    std::vector<std::shared_ptr<Client>> getClientsByState(Client::State state) const;
    std::vector<uint32_t> getClientIds() const;
    
    // Message broadcasting
    size_t broadcastMessage(const MessageHeader& header, const void* data = nullptr);
    size_t sendToMultipleClients(const std::vector<uint32_t>& client_ids, 
                                const MessageHeader& header, const void* data = nullptr);
    
    // Maintenance
    void cleanupDisconnectedClients();
    void sendKeepAlives();
    void checkTimeouts();
    
    // Statistics
    struct Stats {
        size_t total_clients = 0;
        size_t connected_clients = 0;
        size_t handshaking_clients = 0;
        size_t error_clients = 0;
        uint64_t total_messages_sent = 0;
        uint64_t total_messages_received = 0;
        uint64_t total_bytes_sent = 0;
        uint64_t total_bytes_received = 0;
        double avg_latency_ms = 0.0;
    };
    
    Stats getStats() const;
    void resetStats();
    
    // Configuration
    void setDefaultClientConfig(const Client::Config& config);
    const Client::Config& getDefaultClientConfig() const { return m_default_config; }

private:
    void updateStats() const;
    
private:
    mutable std::mutex m_clients_mutex;
    std::unordered_map<uint32_t, std::shared_ptr<Client>> m_clients;
    size_t m_max_clients;
    
    Client::Config m_default_config;
    mutable Stats m_stats;
    mutable std::chrono::steady_clock::time_point m_last_stats_update;
};

/**
 * @brief Utility functions for client handling
 */
namespace ClientUtils {
    // Socket utilities
    std::string getSocketAddress(socket_t socket);
    uint16_t getSocketPort(socket_t socket);
    bool isSocketValid(socket_t socket);
    void closeSocket(socket_t socket);
    
    // Address validation
    bool isValidTcpAddress(const std::string& address);
    bool isValidUnixSocketPath(const std::string& path);
    bool isLocalConnection(socket_t socket);
    
    // Protocol utilities
    std::string capabilitiesToString(uint32_t capabilities);
    std::string stateToString(Client::State state);
    std::string typeToString(Client::Type type);
    
    // Performance utilities
    size_t estimateMessageSize(const MessageHeader& header);
    bool shouldBufferMessage(const MessageHeader& header);
    
    // Security utilities
    bool isClientTrusted(const Client::Info& info);
    bool validateClientName(const std::string& name);
    uint32_t calculateClientPriority(const Client::Info& info);
}

} // namespace Kairos