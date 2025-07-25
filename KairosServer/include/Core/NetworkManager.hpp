// KairosServer/src/Core/NetworkManager.hpp
#pragma once

#include "KairosShared/Protocol.hpp"
#include "Network/Client.hpp"
#include "Graphics/RenderCommand.hpp"
#include <memory>
#include <vector>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <atomic>
#include <functional>
#include <queue>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <sys/un.h>
    #include <arpa/inet.h>
#endif

namespace Kairos {

class Server;

/**
 * @brief Network manager for handling client connections and protocol
 * 
 * Responsibilities:
 * - TCP and Unix socket server management
 * - Client connection handling
 * - Protocol message parsing
 * - Command queuing and distribution
 * - Event broadcasting to clients
 */
class NetworkManager {
public:
    struct Config {
        // TCP configuration
        std::string tcp_bind_address = "127.0.0.1";
        uint16_t tcp_port = DEFAULT_SERVER_PORT;
        bool enable_tcp = true;
        
        // Unix socket configuration
        std::string unix_socket_path = DEFAULT_UNIX_SOCKET;
        bool enable_unix_socket = true;
        
        // Connection limits
        uint32_t max_clients = 32;
        uint32_t max_connections_per_ip = 8;
        
        // Buffer sizes
        size_t receive_buffer_size = 64 * 1024;    // 64KB
        size_t send_buffer_size = 64 * 1024;       // 64KB
        size_t message_queue_size = 10000;
        
        // Timeouts
        uint32_t client_timeout_seconds = 30;
        uint32_t handshake_timeout_seconds = 5;
        
        // Performance
        uint32_t network_thread_count = 2;
        bool use_non_blocking_sockets = true;
        bool enable_tcp_nodelay = true;
        bool enable_keepalive = true;
        
        // Security
        bool require_handshake = true;
        uint32_t max_message_size = 10 * 1024 * 1024; // 10MB
        bool enable_rate_limiting = true;
        uint32_t max_commands_per_second = 10000;
    };
    
    struct Stats {
        // Connection stats
        std::atomic<uint32_t> active_connections{0};
        std::atomic<uint32_t> total_connections{0};
        std::atomic<uint32_t> failed_connections{0};
        std::atomic<uint32_t> timed_out_connections{0};
        
        // Message stats
        std::atomic<uint64_t> messages_received{0};
        std::atomic<uint64_t> messages_sent{0};
        std::atomic<uint64_t> bytes_received{0};
        std::atomic<uint64_t> bytes_sent{0};
        std::atomic<uint32_t> invalid_messages{0};
        
        // Performance stats
        std::atomic<uint32_t> queued_commands{0};
        std::atomic<uint32_t> dropped_commands{0};
        std::atomic<uint32_t> processed_commands{0};
        double avg_message_processing_time_us = 0.0;
        double avg_network_latency_ms = 0.0;
        
        // Per-client stats
        std::unordered_map<uint32_t, uint64_t> client_message_counts;
        std::unordered_map<uint32_t, uint64_t> client_byte_counts;
        std::unordered_map<uint32_t, std::chrono::steady_clock::time_point> client_last_activity;
    };
    
    // Event callbacks
    using ClientConnectedCallback = std::function<void(uint32_t client_id, const std::string& client_info)>;
    using ClientDisconnectedCallback = std::function<void(uint32_t client_id, const std::string& reason)>;
    using CommandReceivedCallback = std::function<void(uint32_t client_id, RenderCommand&& command)>;
    using ErrorCallback = std::function<void(const std::string& error_message, uint32_t client_id)>;

public:
    explicit NetworkManager(const Config& config = Config{});
    ~NetworkManager();
    
    // Lifecycle management
    bool initialize();
    void shutdown();
    bool isRunning() const { return m_running; }
    
    // Server management
    bool startTcpServer();
    bool startUnixSocketServer();
    void stopAllServers();
    
    // Client management
    std::vector<uint32_t> getConnectedClients() const;
    bool disconnectClient(uint32_t client_id, const std::string& reason = "Server request");
    std::shared_ptr<Client> getClient(uint32_t client_id) const;
    
    // Message sending
    bool sendMessage(uint32_t client_id, const MessageHeader& header, const void* data = nullptr);
    bool broadcastMessage(const MessageHeader& header, const void* data = nullptr);
    bool sendToLayer(uint8_t layer_id, const MessageHeader& header, const void* data = nullptr);
    
    // Event sending
    bool sendInputEvent(uint32_t client_id, const InputEvent& event);
    bool sendFrameCallback(uint32_t client_id, const FrameCallback& callback);
    bool sendErrorResponse(uint32_t client_id, ErrorCode error_code, 
                          const std::string& message, uint32_t original_sequence = 0);
    
    // Ping/Pong for latency measurement
    bool sendPing(uint32_t client_id);
    void handlePong(uint32_t client_id, const PongData& pong_data);
    
    // Configuration
    void setConfig(const Config& config);
    const Config& getConfig() const { return m_config; }
    
    // Statistics
    const Stats& getStats() const { return m_stats; }
    void resetStats();
    
    // Event callbacks
    void setClientConnectedCallback(ClientConnectedCallback callback);
    void setClientDisconnectedCallback(ClientDisconnectedCallback callback);
    void setCommandReceivedCallback(CommandReceivedCallback callback);
    void setErrorCallback(ErrorCallback callback);

private:
    // Network thread management
    void networkThreadMain();
    void clientHandlerThread(std::shared_ptr<Client> client);
    
    // Socket management
    bool createTcpSocket();
    bool createUnixSocket();
    void acceptConnections();
    std::shared_ptr<Client> acceptTcpConnection();
    std::shared_ptr<Client> acceptUnixConnection();
    
    // Client lifecycle
    bool handleClientHandshake(std::shared_ptr<Client> client);
    void processClientMessages(std::shared_ptr<Client> client);
    void cleanupClient(uint32_t client_id);
    
    // Message processing
    bool processMessage(std::shared_ptr<Client> client, const MessageHeader& header, 
                       const std::vector<uint8_t>& data);
    RenderCommand convertMessageToCommand(const MessageHeader& header, const std::vector<uint8_t>& data);
    
    // Protocol handlers
    void handleClientHello(std::shared_ptr<Client> client, const ClientHello& hello);
    void handlePing(std::shared_ptr<Client> client, const PingData& ping);
    void handleDisconnect(std::shared_ptr<Client> client);
    
    // Rate limiting
    bool checkRateLimit(uint32_t client_id);
    void updateRateLimits();
    
    // Validation
    bool validateMessage(const MessageHeader& header, const std::vector<uint8_t>& data);
    bool validateClient(uint32_t client_id) const;
    
    // Utilities
    uint32_t generateClientId();
    std::string getClientEndpointInfo(std::shared_ptr<Client> client) const;
    void updateStats();
    void logConnectionEvent(const std::string& event, uint32_t client_id, const std::string& details = "");

private:
    Config m_config;
    Stats m_stats;
    
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_accepting_connections{false};
    
    // Network threads
    std::vector<std::thread> m_network_threads;
    std::thread m_accept_thread;
    
    // Sockets
    int m_tcp_socket = -1;
    int m_unix_socket = -1;
    
    // Client management
    std::unordered_map<uint32_t, std::shared_ptr<Client>> m_clients;
    mutable std::mutex m_clients_mutex;
    std::atomic<uint32_t> m_next_client_id{1};
    
    // Rate limiting
    struct RateLimitInfo {
        uint32_t command_count = 0;
        std::chrono::steady_clock::time_point last_reset;
        bool is_limited = false;
    };
    std::unordered_map<uint32_t, RateLimitInfo> m_rate_limits;
    std::mutex m_rate_limit_mutex;
    
    // Callbacks
    ClientConnectedCallback m_client_connected_callback;
    ClientDisconnectedCallback m_client_disconnected_callback;
    CommandReceivedCallback m_command_received_callback;
    ErrorCallback m_error_callback;
    
    // Performance tracking
    std::chrono::steady_clock::time_point m_last_stats_update;
    mutable std::mutex m_stats_mutex;
    
    // Message queuing (for high load scenarios)
    std::queue<std::pair<uint32_t, RenderCommand>> m_command_queue;
    std::mutex m_command_queue_mutex;
    std::condition_variable m_command_queue_cv;
    
    // Connection tracking
    std::unordered_map<std::string, uint32_t> m_connections_per_ip;
    std::mutex m_connection_tracking_mutex;
    
    // Cleanup management
    std::vector<uint32_t> m_clients_to_cleanup;
    std::mutex m_cleanup_mutex;
};

/**
 * @brief Network protocol utilities
 */
class ProtocolUtils {
public:
    // Message validation
    static bool validateMessageHeader(const MessageHeader& header);
    static bool validateMessageSize(MessageType type, uint32_t data_size);
    static bool validateClientCapabilities(uint32_t capabilities);
    
    // Message creation helpers
    static MessageHeader createHeader(MessageType type, uint32_t client_id = 0, 
                                     uint32_t sequence = 0, uint32_t data_size = 0, 
                                     uint8_t layer_id = 0);
    static std::vector<uint8_t> createMessage(const MessageHeader& header, const void* data = nullptr);
    
    // Protocol conversion
    static ServerHello createServerHello(uint32_t client_id, uint32_t server_version = PROTOCOL_VERSION);
    static ErrorResponse createErrorResponse(ErrorCode error_code, const std::string& message, 
                                           uint32_t original_sequence = 0);
    static PongData createPongResponse(const PingData& ping_data, uint32_t server_load = 0, 
                                      uint32_t queue_depth = 0);
    
    // Network byte order
    static void hostToNetwork(MessageHeader& header);
    static void networkToHost(MessageHeader& header);
    
    // Timestamp utilities
    static uint64_t getCurrentTimestamp();
    static double calculateLatency(uint64_t sent_timestamp, uint64_t received_timestamp);
    
    // Address utilities
    static std::string sockaddrToString(const struct sockaddr* addr);
    static bool isLocalAddress(const std::string& address);
    static bool isValidUnixSocketPath(const std::string& path);
    
private:
    static constexpr uint32_t MAGIC_NUMBER = 0x4B41524F; // "KARO"
};

/**
 * @brief Network security and validation
 */
class NetworkSecurity {
public:
    // Connection validation
    static bool isAddressAllowed(const std::string& address);
    static bool isConnectionLimitExceeded(const std::string& address, uint32_t current_count, uint32_t limit);
    
    // Message validation
    static bool isMessageSizeValid(uint32_t size, uint32_t max_size);
    static bool isMessageRateValid(uint32_t client_id, uint32_t messages_per_second, uint32_t limit);
    
    // Protocol security
    static bool validateHandshake(const ClientHello& hello);
    static bool validateCapabilities(uint32_t requested_capabilities, uint32_t server_capabilities);
    
    // Rate limiting
    static bool checkCommandRate(uint32_t client_id, uint32_t commands_in_window, uint32_t limit);
    static void updateRateWindow(uint32_t client_id);
    
    // Blacklist management
    static void addToBlacklist(const std::string& address, const std::string& reason);
    static bool isBlacklisted(const std::string& address);
    static void removeFromBlacklist(const std::string& address);
    
private:
    static std::unordered_set<std::string> s_blacklisted_addresses;
    static std::mutex s_blacklist_mutex;
    
    static std::unordered_map<uint32_t, std::queue<std::chrono::steady_clock::time_point>> s_rate_windows;
    static std::mutex s_rate_mutex;
};

} // namespace Kairos