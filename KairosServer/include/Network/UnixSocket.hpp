// KairosServer/include/Network/UnixSocket.hpp
#pragma once

#include <Types.hpp>
#include <string>
#include <vector>
#include <chrono>
#include <functional>
#include <thread>
#include <mutex>

#ifndef _WIN32
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
using socket_t = int;
#else
// Unix sockets not supported on Windows
using socket_t = int;
#endif

namespace Kairos {

/**
 * @brief Unix domain socket wrapper (Linux/macOS only)
 */
class UnixSocket {
public:
    enum class State {
        CLOSED,
        CONNECTING,
        CONNECTED,
        LISTENING,
        ERROR
    };
    
    struct Config {
        uint32_t send_buffer_size = 64 * 1024;
        uint32_t receive_buffer_size = 64 * 1024;
        uint32_t connect_timeout_ms = 5000;
        uint32_t send_timeout_ms = 1000;
        uint32_t receive_timeout_ms = 1000;
        bool auto_remove_socket_file = true;
        mode_t socket_permissions = 0666;
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
    UnixSocket();
    explicit UnixSocket(socket_t existing_socket);
    ~UnixSocket();
    
    // Non-copyable but movable
    UnixSocket(const UnixSocket&) = delete;
    UnixSocket& operator=(const UnixSocket&) = delete;
    UnixSocket(UnixSocket&& other) noexcept;
    UnixSocket& operator=(UnixSocket&& other) noexcept;
    
    // Configuration
    void setConfig(const Config& config);
    const Config& getConfig() const { return m_config; }
    
    // Connection management
    bool connect(const std::string& socket_path);
    bool bind(const std::string& socket_path);
    bool listen(int backlog = 32);
    UnixSocket accept();
    void close();
    
    // Data transmission
    int send(const void* data, size_t size);
    int receive(void* buffer, size_t buffer_size);
    
    // File descriptor passing (Unix-specific feature)
    bool sendFileDescriptor(int fd);
    int receiveFileDescriptor();
    
    // Credential passing (Linux-specific)
    struct Credentials {
        pid_t pid;
        uid_t uid;
        gid_t gid;
    };
    bool sendCredentials(const Credentials& creds);
    bool receiveCredentials(Credentials& creds);
    
    // Non-blocking I/O
    bool setNonBlocking(bool enabled);
    bool isDataAvailable(uint32_t timeout_ms = 0);
    bool canSend(uint32_t timeout_ms = 0);
    
    // Socket information
    State getState() const { return m_state; }
    bool isConnected() const { return m_state == State::CONNECTED; }
    bool isListening() const { return m_state == State::LISTENING; }
    bool isValid() const;
    
    std::string getSocketPath() const { return m_socket_path; }
    
    // Statistics
    const Stats& getStats() const { return m_stats; }
    void resetStats();
    
    // Error handling
    int getLastError() const;
    std::string getLastErrorString() const;
    
    // Advanced options
    bool setSendBufferSize(uint32_t size);
    bool setReceiveBufferSize(uint32_t size);
    bool setSendTimeout(uint32_t timeout_ms);
    bool setReceiveTimeout(uint32_t timeout_ms);
    bool setSocketPermissions(mode_t permissions);
    
    // Raw socket access
    socket_t getHandle() const { return m_socket; }
    
    // Platform availability
    static bool isSupported();

private:
    // Internal helpers
    bool createSocket();
    bool applyConfig();
    void updateState(State new_state);
    void updateStats(bool send_operation, size_t bytes, bool success);
    bool removeSocketFile();
    
    // Platform-specific helpers
    bool setSocketOption(int level, int option_name, const void* option_value, socklen_t option_length);
    bool getSocketOption(int level, int option_name, void* option_value, socklen_t* option_length);

private:
    socket_t m_socket;
    State m_state;
    Config m_config;
    Stats m_stats;
    std::string m_socket_path;
    
    // Error tracking
    int m_last_error;
    mutable std::string m_last_error_string;
};

/**
 * @brief Unix socket server for IPC
 */
class UnixSocketServer {
public:
    using ClientConnectedCallback = std::function<void(UnixSocket&& client_socket, const std::string& client_info)>;
    using ClientDisconnectedCallback = std::function<void(const std::string& client_info, const std::string& reason)>;
    using ErrorCallback = std::function<void(const std::string& error_message)>;
    
    struct Config {
        std::string socket_path = "/tmp/kairos_server.sock";
        int listen_backlog = 32;
        uint32_t max_connections = 1000;
        uint32_t accept_timeout_ms = 100;
        mode_t socket_permissions = 0666;
        bool auto_remove_existing = true;
    };

public:
    explicit UnixSocketServer(const Config& config = Config{});
    ~UnixSocketServer();
    
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
    bool disconnectClient(const std::string& client_info);
    
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
    UnixSocket m_listen_socket;
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
 * @brief Unix socket client for IPC
 */
class UnixSocketClient {
public:
    using ConnectedCallback = std::function<void()>;
    using DisconnectedCallback = std::function<void(const std::string& reason)>;
    using DataReceivedCallback = std::function<void(const std::vector<uint8_t>& data)>;
    using ErrorCallback = std::function<void(const std::string& error_message)>;

public:
    UnixSocketClient();
    ~UnixSocketClient();
    
    // Connection management
    bool connect(const std::string& socket_path);
    void disconnect();
    bool isConnected() const;
    
    // Data transmission
    bool send(const std::vector<uint8_t>& data);
    bool send(const void* data, size_t size);
    std::vector<uint8_t> receive(size_t max_size = 4096);
    
    // File descriptor operations
    bool sendFileDescriptor(int fd);
    int receiveFileDescriptor();
    
    // Callbacks
    void setConnectedCallback(ConnectedCallback callback);
    void setDisconnectedCallback(DisconnectedCallback callback);
    void setDataReceivedCallback(DataReceivedCallback callback);
    void setErrorCallback(ErrorCallback callback);
    
    // Configuration
    void setConfig(const UnixSocket::Config& config);
    
    // Statistics
    const UnixSocket::Stats& getStats() const;

private:
    UnixSocket m_socket;
    
    // Callbacks
    ConnectedCallback m_connected_callback;
    DisconnectedCallback m_disconnected_callback;
    DataReceivedCallback m_data_received_callback;
    ErrorCallback m_error_callback;
};

/**
 * @brief Unix socket utilities and helpers
 */
namespace UnixSocketUtils {
    // Path validation
    bool isValidSocketPath(const std::string& path);
    bool socketExists(const std::string& path);
    bool removeSocketFile(const std::string& path);
    
    // Permission management
    bool setSocketPermissions(const std::string& path, mode_t permissions);
    mode_t getSocketPermissions(const std::string& path);
    
    // Process information
    struct ProcessInfo {
        pid_t pid;
        uid_t uid;
        gid_t gid;
        std::string process_name;
        std::string user_name;
        std::string group_name;
    };
    
    bool getProcessInfo(const UnixSocket& socket, ProcessInfo& info);
    bool getCurrentProcessInfo(ProcessInfo& info);
    
    // Socket pair creation (for testing or IPC)
    std::pair<UnixSocket, UnixSocket> createSocketPair();
    
    // Path utilities
    std::string getDefaultSocketPath(const std::string& app_name);
    std::string getTempSocketPath(const std::string& prefix = "kairos");
    bool createSocketDirectory(const std::string& socket_path);
    
    // Testing utilities
    bool testUnixSocketConnection(const std::string& socket_path, uint32_t timeout_ms = 5000);
    
    // Platform detection
    bool isUnixSocketSupported();
    std::string getUnsupportedReason(); // For Windows
}

} // namespace Kairos