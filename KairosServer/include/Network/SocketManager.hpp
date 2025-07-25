// KairosServer/include/Network/SocketManager.hpp
#pragma once

#include <Network/TCPSocket.hpp>
#include <Network/UnixSocket.hpp>
#include <Types.hpp>
#include <unordered_map>
#include <vector>
#include <memory>
#include <thread>
#include <mutex>
#include <atomic>
#include <functional>

namespace Kairos {

/**
 * @brief Unified socket management for TCP and Unix sockets
 */
class SocketManager {
public:
    enum class SocketType {
        TCP,
        UNIX_SOCKET
    };
    
    struct SocketInfo {
        uint32_t socket_id;
        SocketType type;
        std::string address;
        uint16_t port = 0; // Only for TCP
        bool is_connected = false;
        bool is_listening = false;
        std::chrono::steady_clock::time_point created_time;
        std::chrono::steady_clock::time_point last_activity;
        uint64_t bytes_sent = 0;
        uint64_t bytes_received = 0;
    };
    
    struct Config {
        uint32_t max_sockets = 1000;
        uint32_t poll_timeout_ms = 100;
        uint32_t cleanup_interval_seconds = 60;
        bool enable_tcp = true;
        bool enable_unix_sockets = true;
        bool enable_statistics = true;
        size_t default_buffer_size = 64 * 1024;
    };
    
    // Event callbacks
    using SocketConnectedCallback = std::function<void(uint32_t socket_id, const SocketInfo& info)>;
    using SocketDisconnectedCallback = std::function<void(uint32_t socket_id, const std::string& reason)>;
    using DataReceivedCallback = std::function<void(uint32_t socket_id, const std::vector<uint8_t>& data)>;
    using ErrorCallback = std::function<void(uint32_t socket_id, const std::string& error_message)>;

public:
    explicit SocketManager(const Config& config = Config{});
    ~SocketManager();
    
    // Lifecycle management
    bool initialize();
    void shutdown();
    bool isRunning() const { return m_running; }
    
    // TCP socket operations
    uint32_t createTcpSocket();
    bool connectTcp(uint32_t socket_id, const std::string& address, uint16_t port);
    uint32_t createTcpServer(const std::string& bind_address, uint16_t port);
    
    // Unix socket operations
    uint32_t createUnixSocket();
    bool connectUnix(uint32_t socket_id, const std::string& socket_path);
    uint32_t createUnixServer(const std::string& socket_path);
    
    // Common socket operations
    bool closeSocket(uint32_t socket_id);
    bool sendData(uint32_t socket_id, const std::vector<uint8_t>& data);
    bool sendData(uint32_t socket_id, const void* data, size_t size);
    std::vector<uint8_t> receiveData(uint32_t socket_id, size_t max_size = 4096);
    
    // Socket information
    std::vector<uint32_t> getAllSocketIds() const;
    std::vector<uint32_t> getConnectedSockets() const;
    std::vector<uint32_t> getListeningSockets() const;
    SocketInfo getSocketInfo(uint32_t socket_id) const;
    bool isSocketValid(uint32_t socket_id) const;
    SocketType getSocketType(uint32_t socket_id) const;
    
    // Event callbacks
    void setSocketConnectedCallback(SocketConnectedCallback callback);
    void setSocketDisconnectedCallback(SocketDisconnectedCallback callback);
    void setDataReceivedCallback(DataReceivedCallback callback);
    void setErrorCallback(ErrorCallback callback);
    
    // Configuration
    void setConfig(const Config& config);
    const Config& getConfig() const { return m_config; }
    
    // Statistics
    struct Stats {
        uint32_t total_sockets_created = 0;
        uint32_t active_tcp_sockets = 0;
        uint32_t active_unix_sockets = 0;
        uint32_t listening_sockets = 0;
        uint64_t total_bytes_sent = 0;
        uint64_t total_bytes_received = 0;
        uint64_t total_connections = 0;
        uint64_t failed_connections = 0;
        uint32_t poll_operations = 0;
        double avg_poll_time_ms = 0.0;
    };
    
    const Stats& getStats() const { return m_stats; }
    void resetStats();
    
    // Advanced operations
    bool setSocketTimeout(uint32_t socket_id, uint32_t timeout_ms);
    bool setSocketBufferSize(uint32_t socket_id, size_t buffer_size);
    bool setSocketNonBlocking(uint32_t socket_id, bool enabled);
    
    // Batch operations
    void sendToAll(const std::vector<uint8_t>& data);
    void sendToMultiple(const std::vector<uint32_t>& socket_ids, const std::vector<uint8_t>& data);
    void closeAll();
    void closeType(SocketType type);

private:
    // Socket wrapper for unified management
    struct ManagedSocket {
        uint32_t id;
        SocketType type;
        std::unique_ptr<TCPSocket> tcp_socket;
        std::unique_ptr<UnixSocket> unix_socket;
        SocketInfo info;
        std::vector<uint8_t> receive_buffer;
        std::mutex socket_mutex;
        
        ManagedSocket(uint32_t socket_id, SocketType socket_type);
        ~ManagedSocket();
        
        bool isValid() const;
        bool isConnected() const;
        bool send(const void* data, size_t size);
        int receive(void* buffer, size_t buffer_size);
        void updateActivity();
    };
    
    // Internal management
    uint32_t generateSocketId();
    std::shared_ptr<ManagedSocket> getSocket(uint32_t socket_id) const;
    void addSocket(std::shared_ptr<ManagedSocket> socket);
    void removeSocket(uint32_t socket_id);
    
    // Event processing
    void eventLoop();
    void pollSockets();
    void processSocketEvents(std::shared_ptr<ManagedSocket> socket);
    void handleNewConnections();
    void cleanupInactiveSockets();
    
    // Callback dispatching
    void dispatchSocketConnected(uint32_t socket_id, const SocketInfo& info);
    void dispatchSocketDisconnected(uint32_t socket_id, const std::string& reason);
    void dispatchDataReceived(uint32_t socket_id, const std::vector<uint8_t>& data);
    void dispatchError(uint32_t socket_id, const std::string& error_message);

private:
    Config m_config;
    std::atomic<bool> m_running{false};
    std::atomic<uint32_t> m_next_socket_id{1};
    
    // Socket storage
    std::unordered_map<uint32_t, std::shared_ptr<ManagedSocket>> m_sockets;
    mutable std::shared_mutex m_sockets_mutex;
    
    // Event processing
    std::thread m_event_thread;
    std::vector<uint32_t> m_listening_sockets;
    
    // Callbacks
    SocketConnectedCallback m_connected_callback;
    SocketDisconnectedCallback m_disconnected_callback;
    DataReceivedCallback m_data_received_callback;
    ErrorCallback m_error_callback;
    
    // Statistics
    Stats m_stats;
    mutable std::mutex m_stats_mutex;
    std::chrono::steady_clock::time_point m_last_cleanup;
};

/**
 * @brief Socket pool for managing socket resources efficiently
 */
class SocketPool {
public:
    struct PoolConfig {
        uint32_t initial_tcp_sockets = 10;
        uint32_t initial_unix_sockets = 5;
        uint32_t max_tcp_sockets = 100;
        uint32_t max_unix_sockets = 50;
        bool auto_expand = true;
        bool auto_shrink = true;
        uint32_t shrink_threshold_seconds = 300; // 5 minutes
    };

public:
    explicit SocketPool(const PoolConfig& config = PoolConfig{});
    ~SocketPool();
    
    // Socket acquisition and release
    std::unique_ptr<TCPSocket> acquireTcpSocket();
    std::unique_ptr<UnixSocket> acquireUnixSocket();
    void releaseTcpSocket(std::unique_ptr<TCPSocket> socket);
    void releaseUnixSocket(std::unique_ptr<UnixSocket> socket);
    
    // Pool management
    void expandPool(SocketManager::SocketType type, uint32_t count);
    void shrinkPool(SocketManager::SocketType type, uint32_t count);
    void optimizePool();
    
    // Statistics
    struct PoolStats {
        uint32_t available_tcp_sockets = 0;
        uint32_t available_unix_sockets = 0;
        uint32_t total_tcp_sockets = 0;
        uint32_t total_unix_sockets = 0;
        uint32_t tcp_acquisitions = 0;
        uint32_t unix_acquisitions = 0;
        uint32_t pool_hits = 0;
        uint32_t pool_misses = 0;
    };
    
    const PoolStats& getStats() const { return m_stats; }
    void resetStats();

private:
    void createInitialSockets();
    void cleanupUnusedSockets();

private:
    PoolConfig m_config;
    
    // Socket pools
    std::vector<std::unique_ptr<TCPSocket>> m_tcp_pool;
    std::vector<std::unique_ptr<UnixSocket>> m_unix_pool;
    std::mutex m_tcp_mutex;
    std::mutex m_unix_mutex;
    
    // Statistics
    PoolStats m_stats;
    std::chrono::steady_clock::time_point m_last_optimization;
};

/**
 * @brief Connection utilities and helpers
 */
namespace ConnectionUtils {
    // Connection testing
    struct ConnectionTest {
        bool success = false;
        uint32_t latency_ms = 0;
        std::string error_message;
    };
    
    ConnectionTest testTcpConnection(const std::string& address, uint16_t port, uint32_t timeout_ms = 5000);
    ConnectionTest testUnixConnection(const std::string& socket_path, uint32_t timeout_ms = 5000);
    
    // Address utilities
    bool isValidTcpAddress(const std::string& address, uint16_t port);
    bool isValidUnixPath(const std::string& path);
    std::string formatTcpAddress(const std::string& address, uint16_t port);
    
    // Connection monitoring
    struct ConnectionHealth {
        bool is_healthy = false;
        uint32_t response_time_ms = 0;
        uint64_t bytes_transmitted = 0;
        uint32_t error_count = 0;
        std::chrono::steady_clock::time_point last_check;
    };
    
    ConnectionHealth checkConnectionHealth(uint32_t socket_id, SocketManager& manager);
    
    // Batch connection operations
    std::vector<uint32_t> connectToMultiple(SocketManager& manager, 
                                           const std::vector<std::pair<std::string, uint16_t>>& tcp_addresses,
                                           const std::vector<std::string>& unix_paths);
    
    // Connection load balancing
    class ConnectionLoadBalancer {
    public:
        enum class Algorithm {
            ROUND_ROBIN,
            LEAST_CONNECTIONS,
            LEAST_LATENCY,
            RANDOM
        };
        
        void addConnection(uint32_t socket_id, const std::string& address);
        void removeConnection(uint32_t socket_id);
        uint32_t selectConnection(Algorithm algorithm = Algorithm::ROUND_ROBIN);
        void updateConnectionStats(uint32_t socket_id, uint32_t latency_ms, bool success);
        
    private:
        struct ConnectionStats {
            uint32_t socket_id;
            std::string address;
            uint32_t connection_count = 0;
            uint32_t avg_latency_ms = 0;
            uint32_t success_count = 0;
            uint32_t total_requests = 0;
        };
        
        std::vector<ConnectionStats> m_connections;
        size_t m_round_robin_index = 0;
        std::mutex m_mutex;
    };
}

} // namespace Kairos