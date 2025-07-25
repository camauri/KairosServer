// KairosServer/src/Network/TCPSocket.cpp
#include <Network/TCPSocket.hpp>
#include <Utils/Logger.hpp>
#include <cstring>
#include <chrono>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    #define CLOSE_SOCKET(s) closesocket(s)
    #define GET_SOCKET_ERROR() WSAGetLastError()
#else
    #include <unistd.h>
    #include <fcntl.h>
    #include <netinet/tcp.h>
    #include <poll.h>
    #include <errno.h>
    #define CLOSE_SOCKET(s) close(s)
    #define GET_SOCKET_ERROR() errno
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
#endif

namespace Kairos {

TCPSocket::TCPSocket() : m_socket(INVALID_SOCKET), m_state(State::CLOSED), m_last_error(0) {
    m_stats.connect_time = std::chrono::steady_clock::now();
    m_stats.last_activity = m_stats.connect_time;
}

TCPSocket::TCPSocket(socket_t existing_socket) 
    : m_socket(existing_socket), m_state(State::CONNECTED), m_last_error(0) {
    m_stats.connect_time = std::chrono::steady_clock::now();
    m_stats.last_activity = m_stats.connect_time;
}

TCPSocket::~TCPSocket() {
    close();
}

TCPSocket::TCPSocket(TCPSocket&& other) noexcept 
    : m_socket(other.m_socket), m_state(other.m_state), m_config(other.m_config),
      m_stats(other.m_stats), m_last_error(other.m_last_error) {
    other.m_socket = INVALID_SOCKET;
    other.m_state = State::CLOSED;
}

TCPSocket& TCPSocket::operator=(TCPSocket&& other) noexcept {
    if (this != &other) {
        close();
        
        m_socket = other.m_socket;
        m_state = other.m_state;
        m_config = other.m_config;
        m_stats = other.m_stats;
        m_last_error = other.m_last_error;
        
        other.m_socket = INVALID_SOCKET;
        other.m_state = State::CLOSED;
    }
    return *this;
}

void TCPSocket::setConfig(const Config& config) {
    m_config = config;
    if (isValid()) {
        applyConfig();
    }
}

bool TCPSocket::connect(const std::string& address, uint16_t port) {
    if (m_state != State::CLOSED) {
        Logger::warning("Attempted to connect on already connected socket");
        return false;
    }
    
    if (!createSocket()) {
        return false;
    }
    
    updateState(State::CONNECTING);
    m_stats.connection_attempts++;
    
    // Setup address
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, address.c_str(), &addr.sin_addr) <= 0) {
        m_last_error = GET_SOCKET_ERROR();
        Logger::error("Invalid address: {}", address);
        close();
        return false;
    }
    
    // Set non-blocking for timeout support
    bool was_blocking = true;
    if (m_config.connect_timeout_ms > 0) {
        was_blocking = !setNonBlocking(true);
    }
    
    // Attempt connection
    int result = ::connect(m_socket, (struct sockaddr*)&addr, sizeof(addr));
    
    if (result == 0) {
        // Connected immediately
        updateState(State::CONNECTED);
        if (!was_blocking) {
            setNonBlocking(false);
        }
        applyConfig();
        Logger::debug("Connected to {}:{}", address, port);
        return true;
    }
    
#ifdef _WIN32
    bool would_block = (GET_SOCKET_ERROR() == WSAEWOULDBLOCK);
#else
    bool would_block = (GET_SOCKET_ERROR() == EINPROGRESS);
#endif
    
    if (would_block && m_config.connect_timeout_ms > 0) {
        // Wait for connection with timeout
        fd_set write_fds;
        FD_ZERO(&write_fds);
        FD_SET(m_socket, &write_fds);
        
        struct timeval timeout;
        timeout.tv_sec = m_config.connect_timeout_ms / 1000;
        timeout.tv_usec = (m_config.connect_timeout_ms % 1000) * 1000;
        
        result = select(m_socket + 1, nullptr, &write_fds, nullptr, &timeout);
        
        if (result > 0 && FD_ISSET(m_socket, &write_fds)) {
            // Check if connection succeeded
            int error = 0;
            socklen_t len = sizeof(error);
            if (getsockopt(m_socket, SOL_SOCKET, SO_ERROR, (char*)&error, &len) == 0 && error == 0) {
                updateState(State::CONNECTED);
                if (!was_blocking) {
                    setNonBlocking(false);
                }
                applyConfig();
                Logger::debug("Connected to {}:{}", address, port);
                return true;
            }
        }
    }
    
    // Connection failed
    m_last_error = GET_SOCKET_ERROR();
    m_stats.failed_operations++;
    Logger::error("Failed to connect to {}:{}: {}", address, port, getLastErrorString());
    close();
    return false;
}

bool TCPSocket::bind(const std::string& address, uint16_t port) {
    if (m_state != State::CLOSED) {
        Logger::warning("Attempted to bind on non-closed socket");
        return false;
    }
    
    if (!createSocket()) {
        return false;
    }
    
    // Setup address
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    
    if (address == "0.0.0.0" || address.empty()) {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else if (inet_pton(AF_INET, address.c_str(), &addr.sin_addr) <= 0) {
        m_last_error = GET_SOCKET_ERROR();
        Logger::error("Invalid bind address: {}", address);
        close();
        return false;
    }
    
    // Apply socket options before binding
    applyConfig();
    
    // Bind socket
    if (::bind(m_socket, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        m_last_error = GET_SOCKET_ERROR();
        m_stats.failed_operations++;
        Logger::error("Failed to bind to {}:{}: {}", address, port, getLastErrorString());
        close();
        return false;
    }
    
    Logger::debug("Bound to {}:{}", address, port);
    return true;
}

bool TCPSocket::listen(int backlog) {
    if (m_state != State::CLOSED) {
        Logger::warning("Attempted to listen on non-closed socket");
        return false;
    }
    
    if (::listen(m_socket, backlog) == SOCKET_ERROR) {
        m_last_error = GET_SOCKET_ERROR();
        m_stats.failed_operations++;
        Logger::error("Failed to listen: {}", getLastErrorString());
        return false;
    }
    
    updateState(State::LISTENING);
    Logger::debug("Socket listening with backlog {}", backlog);
    return true;
}

TCPSocket TCPSocket::accept() {
    if (m_state != State::LISTENING) {
        Logger::warning("Attempted to accept on non-listening socket");
        return TCPSocket();
    }
    
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    
    socket_t client_socket = ::accept(m_socket, (struct sockaddr*)&client_addr, &addr_len);
    
    if (client_socket == INVALID_SOCKET) {
        m_last_error = GET_SOCKET_ERROR();
#ifdef _WIN32
        if (m_last_error != WSAEWOULDBLOCK) {
#else
        if (m_last_error != EAGAIN && m_last_error != EWOULDBLOCK) {
#endif
            m_stats.failed_operations++;
            Logger::error("Accept failed: {}", getLastErrorString());
        }
        return TCPSocket();
    }
    
    TCPSocket client(client_socket);
    client.setConfig(m_config);
    client.applyConfig();
    
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
    uint16_t client_port = ntohs(client_addr.sin_port);
    
    Logger::debug("Accepted connection from {}:{}", client_ip, client_port);
    return client;
}

void TCPSocket::close() {
    if (m_socket != INVALID_SOCKET) {
        CLOSE_SOCKET(m_socket);
        m_socket = INVALID_SOCKET;
    }
    updateState(State::CLOSED);
}

int TCPSocket::send(const void* data, size_t size) {
    if (!isValid() || !data || size == 0) {
        return -1;
    }
    
    m_stats.send_operations++;
    
    int result = ::send(m_socket, static_cast<const char*>(data), static_cast<int>(size), MSG_NOSIGNAL);
    
    if (result > 0) {
        m_stats.bytes_sent += result;
        m_stats.last_activity = std::chrono::steady_clock::now();
        updateStats(true, result, true);
    } else {
        m_last_error = GET_SOCKET_ERROR();
        updateStats(true, 0, false);
        
#ifdef _WIN32
        if (m_last_error != WSAEWOULDBLOCK) {
#else
        if (m_last_error != EAGAIN && m_last_error != EWOULDBLOCK) {
#endif
            Logger::debug("Send failed: {}", getLastErrorString());
            updateState(State::ERROR);
        }
    }
    
    return result;
}

int TCPSocket::receive(void* buffer, size_t buffer_size) {
    if (!isValid() || !buffer || buffer_size == 0) {
        return -1;
    }
    
    m_stats.receive_operations++;
    
    int result = ::recv(m_socket, static_cast<char*>(buffer), static_cast<int>(buffer_size), 0);
    
    if (result > 0) {
        m_stats.bytes_received += result;
        m_stats.last_activity = std::chrono::steady_clock::now();
        updateStats(false, result, true);
    } else if (result == 0) {
        // Connection closed by peer
        Logger::debug("Connection closed by peer");
        updateState(State::CLOSED);
    } else {
        m_last_error = GET_SOCKET_ERROR();
        updateStats(false, 0, false);
        
#ifdef _WIN32
        if (m_last_error != WSAEWOULDBLOCK) {
#else
        if (m_last_error != EAGAIN && m_last_error != EWOULDBLOCK) {
#endif
            Logger::debug("Receive failed: {}", getLastErrorString());
            updateState(State::ERROR);
        }
    }
    
    return result;
}

bool TCPSocket::setNonBlocking(bool enabled) {
    if (!isValid()) {
        return false;
    }
    
#ifdef _WIN32
    u_long mode = enabled ? 1 : 0;
    if (ioctlsocket(m_socket, FIONBIO, &mode) != 0) {
        m_last_error = GET_SOCKET_ERROR();
        Logger::error("Failed to set non-blocking mode: {}", getLastErrorString());
        return false;
    }
#else
    int flags = fcntl(m_socket, F_GETFL, 0);
    if (flags == -1) {
        m_last_error = GET_SOCKET_ERROR();
        Logger::error("Failed to get socket flags: {}", getLastErrorString());
        return false;
    }
    
    if (enabled) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }
    
    if (fcntl(m_socket, F_SETFL, flags) == -1) {
        m_last_error = GET_SOCKET_ERROR();
        Logger::error("Failed to set non-blocking mode: {}", getLastErrorString());
        return false;
    }
#endif
    
    return true;
}

bool TCPSocket::isDataAvailable(uint32_t timeout_ms) {
    if (!isValid()) {
        return false;
    }
    
#ifdef _WIN32
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(m_socket, &read_fds);
    
    struct timeval timeout;
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    
    int result = select(0, &read_fds, nullptr, nullptr, &timeout);
    return result > 0 && FD_ISSET(m_socket, &read_fds);
#else
    struct pollfd pfd;
    pfd.fd = m_socket;
    pfd.events = POLLIN;
    pfd.revents = 0;
    
    int result = poll(&pfd, 1, static_cast<int>(timeout_ms));
    return result > 0 && (pfd.revents & POLLIN);
#endif
}

bool TCPSocket::canSend(uint32_t timeout_ms) {
    if (!isValid()) {
        return false;
    }
    
#ifdef _WIN32
    fd_set write_fds;
    FD_ZERO(&write_fds);
    FD_SET(m_socket, &write_fds);
    
    struct timeval timeout;
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    
    int result = select(0, nullptr, &write_fds, nullptr, &timeout);
    return result > 0 && FD_ISSET(m_socket, &write_fds);
#else
    struct pollfd pfd;
    pfd.fd = m_socket;
    pfd.events = POLLOUT;
    pfd.revents = 0;
    
    int result = poll(&pfd, 1, static_cast<int>(timeout_ms));
    return result > 0 && (pfd.revents & POLLOUT);
#endif
}

bool TCPSocket::isValid() const {
    return m_socket != INVALID_SOCKET;
}

std::string TCPSocket::getLocalAddress() const {
    if (!isValid()) {
        return "";
    }
    
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    
    if (getsockname(m_socket, (struct sockaddr*)&addr, &addr_len) == 0) {
        char ip_str[INET_ADDRSTRLEN];
        if (inet_ntop(AF_INET, &addr.sin_addr, ip_str, INET_ADDRSTRLEN)) {
            return std::string(ip_str);
        }
    }
    
    return "";
}

uint16_t TCPSocket::getLocalPort() const {
    if (!isValid()) {
        return 0;
    }
    
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    
    if (getsockname(m_socket, (struct sockaddr*)&addr, &addr_len) == 0) {
        return ntohs(addr.sin_port);
    }
    
    return 0;
}

std::string TCPSocket::getRemoteAddress() const {
    if (!isValid()) {
        return "";
    }
    
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    
    if (getpeername(m_socket, (struct sockaddr*)&addr, &addr_len) == 0) {
        char ip_str[INET_ADDRSTRLEN];
        if (inet_ntop(AF_INET, &addr.sin_addr, ip_str, INET_ADDRSTRLEN)) {
            return std::string(ip_str);
        }
    }
    
    return "";
}

uint16_t TCPSocket::getRemotePort() const {
    if (!isValid()) {
        return 0;
    }
    
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    
    if (getpeername(m_socket, (struct sockaddr*)&addr, &addr_len) == 0) {
        return ntohs(addr.sin_port);
    }
    
    return 0;
}

void TCPSocket::resetStats() {
    m_stats = Stats{};
    m_stats.connect_time = std::chrono::steady_clock::now();
    m_stats.last_activity = m_stats.connect_time;
}

int TCPSocket::getLastError() const {
    return m_last_error;
}

std::string TCPSocket::getLastErrorString() const {
    if (m_last_error_string.empty()) {
#ifdef _WIN32
        char* msg_buf = nullptr;
        size_t size = FormatMessageA(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr, m_last_error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            (LPSTR)&msg_buf, 0, nullptr);
        
        if (size && msg_buf) {
            m_last_error_string = std::string(msg_buf, size);
            LocalFree(msg_buf);
            
            // Remove trailing newlines
            while (!m_last_error_string.empty() && 
                   (m_last_error_string.back() == '\n' || m_last_error_string.back() == '\r')) {
                m_last_error_string.pop_back();
            }
        } else {
            m_last_error_string = "Unknown error " + std::to_string(m_last_error);
        }
#else
        m_last_error_string = std::strerror(m_last_error);
#endif
    }
    
    return m_last_error_string;
}

bool TCPSocket::setKeepAlive(bool enabled, uint32_t idle_time, uint32_t interval, uint32_t count) {
    if (!isValid()) {
        return false;
    }
    
    int opt = enabled ? 1 : 0;
    if (!setSocketOption(SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt))) {
        return false;
    }
    
    if (enabled) {
#ifdef _WIN32
        tcp_keepalive ka;
        ka.onoff = 1;
        ka.keepalivetime = idle_time * 1000;  // Convert to milliseconds
        ka.keepaliveinterval = interval * 1000;
        
        DWORD bytes_returned;
        if (WSAIoctl(m_socket, SIO_KEEPALIVE_VALS, &ka, sizeof(ka), 
                     nullptr, 0, &bytes_returned, nullptr, nullptr) == SOCKET_ERROR) {
            m_last_error = GET_SOCKET_ERROR();
            return false;
        }
#else
        int idle = static_cast<int>(idle_time);
        int intvl = static_cast<int>(interval);
        int cnt = static_cast<int>(count);
        
        if (!setSocketOption(IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle)) ||
            !setSocketOption(IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl)) ||
            !setSocketOption(IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt))) {
            return false;
        }
#endif
    }
    
    return true;
}

bool TCPSocket::setTcpNoDelay(bool enabled) {
    if (!isValid()) {
        return false;
    }
    
    int opt = enabled ? 1 : 0;
    return setSocketOption(IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
}

bool TCPSocket::setReuseAddress(bool enabled) {
    if (!isValid()) {
        return false;
    }
    
    int opt = enabled ? 1 : 0;
    return setSocketOption(SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
}

bool TCPSocket::setSendBufferSize(uint32_t size) {
    if (!isValid()) {
        return false;
    }
    
    int buf_size = static_cast<int>(size);
    return setSocketOption(SOL_SOCKET, SO_SNDBUF, &buf_size, sizeof(buf_size));
}

bool TCPSocket::setReceiveBufferSize(uint32_t size) {
    if (!isValid()) {
        return false;
    }
    
    int buf_size = static_cast<int>(size);
    return setSocketOption(SOL_SOCKET, SO_RCVBUF, &buf_size, sizeof(buf_size));
}

bool TCPSocket::setSendTimeout(uint32_t timeout_ms) {
    if (!isValid()) {
        return false;
    }
    
#ifdef _WIN32
    DWORD timeout = timeout_ms;
    return setSocketOption(SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#else
    struct timeval timeout;
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    return setSocketOption(SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#endif
}

bool TCPSocket::setReceiveTimeout(uint32_t timeout_ms) {
    if (!isValid()) {
        return false;
    }
    
#ifdef _WIN32
    DWORD timeout = timeout_ms;
    return setSocketOption(SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#else
    struct timeval timeout;
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    return setSocketOption(SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif
}

// Private methods implementation

bool TCPSocket::createSocket() {
    m_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    
    if (m_socket == INVALID_SOCKET) {
        m_last_error = GET_SOCKET_ERROR();
        Logger::error("Failed to create socket: {}", getLastErrorString());
        return false;
    }
    
    return true;
}

bool TCPSocket::applyConfig() {
    if (!isValid()) {
        return false;
    }
    
    bool success = true;
    
    // Apply all configuration options
    if (!setReuseAddress(m_config.enable_reuseaddr)) {
        Logger::warning("Failed to set SO_REUSEADDR");
        success = false;
    }
    
    if (!setTcpNoDelay(m_config.enable_nodelay)) {
        Logger::warning("Failed to set TCP_NODELAY");
        success = false;
    }
    
    if (!setKeepAlive(m_config.enable_keepalive)) {
        Logger::warning("Failed to set SO_KEEPALIVE");
        success = false;
    }
    
    if (!setSendBufferSize(m_config.send_buffer_size)) {
        Logger::warning("Failed to set send buffer size");
        success = false;
    }
    
    if (!setReceiveBufferSize(m_config.receive_buffer_size)) {
        Logger::warning("Failed to set receive buffer size");
        success = false;
    }
    
    if (!setSendTimeout(m_config.send_timeout_ms)) {
        Logger::warning("Failed to set send timeout");
        success = false;
    }
    
    if (!setReceiveTimeout(m_config.receive_timeout_ms)) {
        Logger::warning("Failed to set receive timeout");
        success = false;
    }
    
    return success;
}

void TCPSocket::updateState(State new_state) {
    if (m_state != new_state) {
        Logger::debug("Socket state changed: {} -> {}", 
                     static_cast<int>(m_state), static_cast<int>(new_state));
        m_state = new_state;
    }
}

void TCPSocket::updateStats(bool send_operation, size_t bytes, bool success) {
    if (!success) {
        m_stats.failed_operations++;
    }
    
    // Update activity timestamp on successful operations
    if (success && bytes > 0) {
        m_stats.last_activity = std::chrono::steady_clock::now();
    }
}

bool TCPSocket::setSocketOption(int level, int option_name, const void* option_value, int option_length) {
    if (!isValid()) {
        return false;
    }
    
    if (setsockopt(m_socket, level, option_name, 
                   static_cast<const char*>(option_value), option_length) == SOCKET_ERROR) {
        m_last_error = GET_SOCKET_ERROR();
        Logger::debug("Failed to set socket option {}: {}", option_name, getLastErrorString());
        return false;
    }
    
    return true;
}

bool TCPSocket::getSocketOption(int level, int option_name, void* option_value, int* option_length) {
    if (!isValid()) {
        return false;
    }
    
    socklen_t len = static_cast<socklen_t>(*option_length);
    if (getsockopt(m_socket, level, option_name, static_cast<char*>(option_value), &len) == SOCKET_ERROR) {
        m_last_error = GET_SOCKET_ERROR();
        Logger::debug("Failed to get socket option {}: {}", option_name, getLastErrorString());
        return false;
    }
    
    *option_length = static_cast<int>(len);
    return true;
}

std::string TCPSocket::formatAddress(const struct sockaddr* addr) const {
    if (addr->sa_family == AF_INET) {
        const struct sockaddr_in* addr_in = reinterpret_cast<const struct sockaddr_in*>(addr);
        char ip_str[INET_ADDRSTRLEN];
        if (inet_ntop(AF_INET, &addr_in->sin_addr, ip_str, INET_ADDRSTRLEN)) {
            return std::string(ip_str) + ":" + std::to_string(ntohs(addr_in->sin_port));
        }
    }
    return "unknown";
}

// TCPServer implementation

TCPServer::TCPServer(const Config& config) : m_config(config), m_running(false) {
    m_stats.start_time = std::chrono::steady_clock::now();
}

TCPServer::~TCPServer() {
    if (m_running) {
        stop();
    }
}

bool TCPServer::start() {
    if (m_running) {
        Logger::warning("TCP server already running");
        return true;
    }
    
    Logger::info("Starting TCP server on {}:{}", m_config.bind_address, m_config.port);
    
    // Configure and bind socket
    TCPSocket::Config socket_config;
    socket_config.enable_reuseaddr = true;
    socket_config.enable_nodelay = m_config.enable_nodelay;
    socket_config.enable_keepalive = m_config.enable_keepalive;
    
    m_listen_socket.setConfig(socket_config);
    
    if (!m_listen_socket.bind(m_config.bind_address, m_config.port)) {
        Logger::error("Failed to bind TCP server");
        return false;
    }
    
    if (!m_listen_socket.listen(m_config.listen_backlog)) {
        Logger::error("Failed to listen on TCP server");
        return false;
    }
    
    // Set non-blocking for timeout support
    m_listen_socket.setNonBlocking(true);
    
    m_running = true;
    m_server_thread = std::thread(&TCPServer::serverLoop, this);
    
    Logger::info("TCP server started successfully");
    return true;
}

void TCPServer::stop() {
    if (!m_running) {
        return;
    }
    
    Logger::info("Stopping TCP server...");
    m_running = false;
    
    if (m_server_thread.joinable()) {
        m_server_thread.join();
    }
    
    m_listen_socket.close();
    
    Logger::info("TCP server stopped");
}

void TCPServer::setConfig(const Config& config) {
    if (m_running) {
        Logger::warning("Cannot change TCP server configuration while running");
        return;
    }
    m_config = config;
}

void TCPServer::setClientConnectedCallback(ClientConnectedCallback callback) {
    m_client_connected_callback = callback;
}

void TCPServer::setClientDisconnectedCallback(ClientDisconnectedCallback callback) {
    m_client_disconnected_callback = callback;
}

void TCPServer::setErrorCallback(ErrorCallback callback) {
    m_error_callback = callback;
}

size_t TCPServer::getConnectionCount() const {
    std::lock_guard<std::mutex> lock(m_stats_mutex);
    return m_stats.active_connections;
}

std::vector<std::string> TCPServer::getConnectedClients() const {
    // In a full implementation, this would maintain a list of connected clients
    return {};
}

bool TCPServer::disconnectClient(const std::string& client_address) {
    // In a full implementation, this would find and disconnect specific client
    Logger::info("Disconnect request for client: {}", client_address);
    return false;
}

void TCPServer::resetStats() {
    std::lock_guard<std::mutex> lock(m_stats_mutex);
    m_stats = Stats{};
    m_stats.start_time = std::chrono::steady_clock::now();
}

void TCPServer::serverLoop() {
    Logger::debug("TCP server loop started");
    
    while (m_running) {
        try {
            handleNewConnection();
            std::this_thread::sleep_for(std::chrono::milliseconds(m_config.accept_timeout_ms));
        } catch (const std::exception& e) {
            Logger::error("Exception in TCP server loop: {}", e.what());
            if (m_error_callback) {
                m_error_callback("Server loop exception: " + std::string(e.what()));
            }
        }
    }
    
    Logger::debug("TCP server loop ended");
}

void TCPServer::handleNewConnection() {
    if (!m_listen_socket.isDataAvailable(0)) {
        return;
    }
    
    TCPSocket client_socket = m_listen_socket.accept();
    if (!client_socket.isValid()) {
        return;
    }
    
    std::string client_address = client_socket.getRemoteAddress() + ":" + 
                                std::to_string(client_socket.getRemotePort());
    
    {
        std::lock_guard<std::mutex> lock(m_stats_mutex);
        m_stats.total_connections++;
        m_stats.active_connections++;
    }
    
    if (m_client_connected_callback) {
        m_client_connected_callback(std::move(client_socket), client_address);
    }
    
    Logger::debug("New client connected: {}", client_address);
}

// TCPClient implementation

TCPClient::TCPClient() = default;

TCPClient::~TCPClient() {
    disconnect();
}

bool TCPClient::connect(const std::string& address, uint16_t port) {
    disconnect();
    
    if (m_socket.connect(address, port)) {
        if (m_connected_callback) {
            m_connected_callback();
        }
        return true;
    }
    
    return false;
}

void TCPClient::disconnect() {
    if (m_socket.isConnected()) {
        if (m_disconnected_callback) {
            m_disconnected_callback("Client disconnect");
        }
    }
    
    m_socket.close();
}

bool TCPClient::isConnected() const {
    return m_socket.isConnected();
}

bool TCPClient::send(const std::vector<uint8_t>& data) {
    return send(data.data(), data.size());
}

bool TCPClient::send(const void* data, size_t size) {
    if (!isConnected()) {
        return false;
    }
    
    int result = m_socket.send(data, size);
    if (result <= 0) {
        if (m_error_callback) {
            m_error_callback("Send failed: " + m_socket.getLastErrorString());
        }
        return false;
    }
    
    return true;
}

std::vector<uint8_t> TCPClient::receive(size_t max_size) {
    std::vector<uint8_t> buffer(max_size);
    
    int received = m_socket.receive(buffer.data(), buffer.size());
    if (received > 0) {
        buffer.resize(received);
        
        if (m_data_received_callback) {
            m_data_received_callback(buffer);
        }
        
        return buffer;
    } else if (received == 0) {
        // Connection closed
        if (m_disconnected_callback) {
            m_disconnected_callback("Connection closed by peer");
        }
    } else {
        // Error occurred
        if (m_error_callback) {
            m_error_callback("Receive failed: " + m_socket.getLastErrorString());
        }
    }
    
    return {};
}

void TCPClient::setConnectedCallback(ConnectedCallback callback) {
    m_connected_callback = callback;
}

void TCPClient::setDisconnectedCallback(DisconnectedCallback callback) {
    m_disconnected_callback = callback;
}

void TCPClient::setDataReceivedCallback(DataReceivedCallback callback) {
    m_data_received_callback = callback;
}

void TCPClient::setErrorCallback(ErrorCallback callback) {
    m_error_callback = callback;
}

void TCPClient::setConfig(const TCPSocket::Config& config) {
    m_socket.setConfig(config);
}

const TCPSocket::Stats& TCPClient::getStats() const {
    return m_socket.getStats();
}

// TCPUtils implementation

namespace TCPUtils {

bool isValidIPAddress(const std::string& address) {
    struct sockaddr_in sa;
    return inet_pton(AF_INET, address.c_str(), &sa.sin_addr) == 1;
}

bool isValidPort(uint16_t port) {
    return port > 0; // Port 0 is reserved
}

std::pair<std::string, uint16_t> parseAddress(const std::string& address_port) {
    size_t colon_pos = address_port.find_last_of(':');
    if (colon_pos == std::string::npos) {
        return {"", 0};
    }
    
    std::string address = address_port.substr(0, colon_pos);
    std::string port_str = address_port.substr(colon_pos + 1);
    
    try {
        uint16_t port = static_cast<uint16_t>(std::stoi(port_str));
        return {address, port};
    } catch (...) {
        return {"", 0};
    }
}

std::vector<std::string> getLocalIPAddresses() {
    std::vector<std::string> addresses;
    
    // Basic implementation - in practice would enumerate network interfaces
    addresses.push_back("127.0.0.1");
    addresses.push_back("0.0.0.0");
    
    return addresses;
}

std::string getPublicIPAddress() {
    // This would typically make an HTTP request to a service
    // For now, return empty string
    return "";
}

bool isLocalAddress(const std::string& address) {
    return address == "127.0.0.1" || 
           address == "localhost" || 
           address.starts_with("192.168.") ||
           address.starts_with("10.") ||
           address.starts_with("172.");
}

bool isPrivateAddress(const std::string& address) {
    return address.starts_with("192.168.") ||
           address.starts_with("10.") ||
           (address.starts_with("172.") && address.length() > 4);
}

bool testConnection(const std::string& address, uint16_t port, uint32_t timeout_ms) {
    TCPSocket socket;
    TCPSocket::Config config;
    config.connect_timeout_ms = timeout_ms;
    socket.setConfig(config);
    
    return socket.connect(address, port);
}

uint32_t measureLatency(const std::string& address, uint16_t port) {
    auto start = std::chrono::steady_clock::now();
    
    bool connected = testConnection(address, port, 5000);
    
    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    return connected ? static_cast<uint32_t>(duration.count()) : UINT32_MAX;
}

bool initializeNetworking() {
#ifdef _WIN32
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        Logger::error("WSAStartup failed: {}", result);
        return false;
    }
    return true;
#else
    return true; // No initialization needed on Unix
#endif
}

void cleanupNetworking() {
#ifdef _WIN32
    WSACleanup();
#endif
}

std::string getLastNetworkError() {
#ifdef _WIN32
    int error = WSAGetLastError();
    char* msg_buf = nullptr;
    size_t size = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR)&msg_buf, 0, nullptr);
    
    std::string result;
    if (size && msg_buf) {
        result = std::string(msg_buf, size);
        LocalFree(msg_buf);
    } else {
        result = "Unknown error " + std::to_string(error);
    }
    return result;
#else
    return std::strerror(errno);
#endif
}

} // namespace TCPUtils

} // namespace Kairos