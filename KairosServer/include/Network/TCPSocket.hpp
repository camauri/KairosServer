// KairosServer/include/Network/TCPSocket.hpp
#pragma once

#include <Types.hpp>
#include <string>
#include <vector>
#include <chrono>
#include <functional>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    using socket_t = SOCKET;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    using socket_t = int;
#endif

namespace Kairos {

/**
 * @brief TCP socket wrapper for cross-platform networking
 */
class TCPSocket {
public:
    enum class State {
        CLOSED,
        CONNECTING,
        CONNECTED,
        LISTENING,
        ERROR
    };
    
    struct Config {
        bool enable_keepalive = true;
        bool enable_nodelay = true;    // Disable Nagle's algorithm
        bool enable_reuseaddr = true;
        uint32_t send_buffer_size = 64 * 1024;
        uint32_t receive_buffer_size = 64 * 1024;
        uint32_t connect_timeout_ms = 5000;
        uint32_t send_timeout_ms = 1000;
        uint32_t receive_timeout_ms = 1000;
    };
    
    struct Stats {
        uint64_t bytes_sent = 0;
        uint64_t bytes_received = 0;
        uint64_t send_operations = 0;
        uint64_t receive_operations = 0;
        uint64_t connection_attempts = 0;
        uint64_t failed_operations = 0;
        std::chrono::steady_clock::time_point connect_time;
        std::chrono::steady_clock::time_point last_activity;
    };

public:
    TCPSocket();
    explicit TCPSocket(socket_t existing_socket);
    ~TCPSocket();
    
    // Non-copyable but movable
    TCPSocket(const TCPSocket&) = delete;
    TCPSocket& operator=(const TCPSocket&) = delete;
    TCPSocket(TCPSocket&& other) noexcept;
    TCPSocket& operator=(TCPSocket&& other) noexcept;
    
    // Configuration
    void setConfig(const Config& config);
    const Config& getConfig() const { return m_config; }
    
    // Connection management
    bool connect(const std::string& address, uint16_t port);
    bool bind(const std::string& address, uint16_t port);
    bool listen(int backlog = 32);
    TCPSocket accept();
    void close();
    
    // Data transmission
    int send(const void* data, size_t size);
    int receive(void* buffer, size_t buffer_size);
    
    // Non-blocking I/O
    bool setNonBlocking(bool enabled);
    bool isDataAvailable(uint32_t timeout_ms = 0);
    bool canSend(uint32_t timeout_ms = 0);
    
    // Socket information
    State getState() const { return m_state; }
    bool isConnected() const { return m_state == State::CONNECTED; }
    bool isListening() const { return m_state == State::LISTENING; }
    bool isValid() const;
    
    std::string getLocalAddress() const;
    uint16_t getLocalPort() const;
    std::string getRemoteAddress() const;
    uint16_t getRemotePort() const;
    
    // Statistics
    const Stats& getStats() const { return m_stats; }
    void resetStats();
    
    // Error handling
    int getLastError() const;
    std::string getLastErrorString() const;
    
    // Advanced options
    bool setKeepAlive(bool enabled, uint32_t idle_time = 7200, uint32_t interval = 75, uint32_t count = 9);
    bool setTcpNoDelay(bool enabled);
    bool setReuseAddress(bool enabled);
    bool setSendBufferSize(uint32_t size);
    bool setReceiveBufferSize(uint32_t size);
    bool setSendTimeout(uint32_t timeout_ms);
    bool setReceiveTimeout(uint32_t timeout_ms);
    
    // Raw socket access (use with caution)
    socket_t getHandle() const { return m_socket; }

private:
    // Internal helpers
    bool createSocket();
    bool applyConfig();
    void updateState(State new_state);
    void updateStats(bool send_operation, size_t bytes, bool success);
    
    // Platform-specific helpers
    bool setSocketOption(int level, int option_name, const void* option_value, int option_length);
    bool getSocketOption(int level, int option_name, void* option_value, int* option_length);
    std::string formatAddress(const struct sockaddr* addr) const;

private:
    socket_t m_socket;
    State m_state;
    Config m_config;
    Stats m_stats;
    
    // Error tracking
    int m_last_error;
    mutable std::string m_last_error_string;
};

/**
 * @brief TCP server for handling multiple client connections
 */
class TCPServer {
public:
    using ClientConnectedCallback = std::function<void(TCPSocket&& client_socket, const std::string& client_address)>;
    using ClientDisconnectedCallback = std::function<void(const std::string& client_address, const std::string& reason)>;
    using ErrorCallback = std::function<void(const std::string& error_message)>;
    
    struct Config {
        std::string bind_address = "0.0.0.0";
        uint16_t port = 8080;
        int listen_backlog = 32;
        uint32_t max_connections = 1000;
        bool enable_keepalive = true;
        bool enable_nodelay = true;
        uint32_t accept_timeout_ms = 100;
    };

public:
    explicit TCPServer(const Config& config = Config{});
    ~TCPServer();
    
    // Server lifecycle
    bool start();
    void stop();
    bool isRunning() const { return m_running; }
    
    // Configuration
    void setConfig(const Config& config);
    const Config& getConfig() const { return m_config; }
    
    // Callbacks
    void setClientConnectedCallback(ClientConnectedCallback callback);
    void setClientDisconnectedCallback(ClientDisconnectedCallback callback);
    void setErrorCallback(ErrorCallback callback);
    
    // Connection management
    size_t getConnectionCount() const;
    std::vector<std::string> getConnectedClients() const;
    bool disconnectClient(const std::string& client_address);
    
    // Statistics
    struct Stats {
        uint64_t total_connections = 0;
        uint64_t active_connections = 0;
        uint64_t failed_connections = 0;
        uint64_t bytes_sent = 0;
        uint64_t bytes_received = 0;
        std::chrono::steady_clock::time_point start_time;
    };
    
    const Stats& getStats() const { return m_stats; }
    void resetStats();

private:
    // Internal server loop
    void serverLoop();
    void handleNewConnection();
    
private:
    Config m_config;
    TCPSocket m_listen_socket;
    bool m_running = false;
    std::thread m_server_thread;
    
    // Callbacks
    ClientConnectedCallback m_client_connected_callback;
    ClientDisconnectedCallback m_client_disconnected_callback;
    ErrorCallback m_error_callback;
    
    // Statistics
    Stats m_stats;
    mutable std::mutex m_stats_mutex;
};

/**
 * @brief TCP client for connecting to servers
 */
class TCPClient {
public:
    using ConnectedCallback = std::function<void()>;
    using DisconnectedCallback = std::function<void(const std::string& reason)>;
    using DataReceivedCallback = std::function<void(const std::vector<uint8_t>& data)>;
    using ErrorCallback = std::function<void(const std::string& error_message)>;

public:
    TCPClient();
    ~TCPClient();
    
    // Connection management
    bool connect(const std::string& address, uint16_t port);
    void disconnect();
    bool isConnected() const;
    
    // Data transmission
    bool send(const std::vector<uint8_t>& data);
    bool send(const void* data, size_t size);
    std::vector<uint8_t> receive(size_t max_size = 4096);
    
    // Callbacks
    void setConnectedCallback(ConnectedCallback callback);
    void setDisconnectedCallback(DisconnectedCallback callback);
    void setDataReceivedCallback(DataReceivedCallback callback);
    void setErrorCallback(ErrorCallback callback);
    
    // Configuration
    void setConfig(const TCPSocket::Config& config);
    
    // Statistics
    const TCPSocket::Stats& getStats() const;

private:
    TCPSocket m_socket;
    
    // Callbacks
    ConnectedCallback m_connected_callback;
    DisconnectedCallback m_disconnected_callback;
    DataReceivedCallback m_data_received_callback;
    ErrorCallback m_error_callback;
};

/**
 * @brief TCP utilities and helpers
 */
namespace TCPUtils {
    // Address validation and parsing
    bool isValidIPAddress(const std::string& address);
    bool isValidPort(uint16_t port);
    std::pair<std::string, uint16_t> parseAddress(const std::string& address_port);
    
    // Network information
    std::vector<std::string> getLocalIPAddresses();
    std::string getPublicIPAddress();
    bool isLocalAddress(const std::string& address);
    bool isPrivateAddress(const std::string& address);
    
    // Connection testing
    bool testConnection(const std::string& address, uint16_t port, uint32_t timeout_ms = 5000);
    uint32_t measureLatency(const std::string& address, uint16_t port);
    
    // Socket utilities
    bool initializeNetworking(); // For Windows WSA startup
    void cleanupNetworking();    // For Windows WSA cleanup
    std::string getLastNetworkError();
}

} // namespace Kairos