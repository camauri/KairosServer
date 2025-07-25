// KairosServer/src/Network/UnixSocket.cpp
#include <Network/UnixSocket.hpp>
#include <Utils/Logger.hpp>
#include <cstring>
#include <filesystem>

#ifndef _WIN32
    #include <sys/socket.h>
    #include <sys/un.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <poll.h>
    #include <errno.h>
    #include <pwd.h>
    #include <grp.h>
    #include <sys/stat.h>
    #include <sys/types.h>
#endif

namespace Kairos {

#ifndef _WIN32

UnixSocket::UnixSocket() : m_socket(-1), m_state(State::CLOSED), m_last_error(0) {
    m_stats.connect_time = std::chrono::steady_clock::now();
    m_stats.last_activity = m_stats.connect_time;
}

UnixSocket::UnixSocket(socket_t existing_socket) 
    : m_socket(existing_socket), m_state(State::CONNECTED), m_last_error(0) {
    m_stats.connect_time = std::chrono::steady_clock::now();
    m_stats.last_activity = m_stats.connect_time;
}

UnixSocket::~UnixSocket() {
    close();
}

UnixSocket::UnixSocket(UnixSocket&& other) noexcept 
    : m_socket(other.m_socket), m_state(other.m_state), m_config(other.m_config),
      m_stats(other.m_stats), m_socket_path(std::move(other.m_socket_path)),
      m_last_error(other.m_last_error) {
    other.m_socket = -1;
    other.m_state = State::CLOSED;
    other.m_socket_path.clear();
}

UnixSocket& UnixSocket::operator=(UnixSocket&& other) noexcept {
    if (this != &other) {
        close();
        
        m_socket = other.m_socket;
        m_state = other.m_state;
        m_config = other.m_config;
        m_stats = other.m_stats;
        m_socket_path = std::move(other.m_socket_path);
        m_last_error = other.m_last_error;
        
        other.m_socket = -1;
        other.m_state = State::CLOSED;
        other.m_socket_path.clear();
    }
    return *this;
}

void UnixSocket::setConfig(const Config& config) {
    m_config = config;
    if (isValid()) {
        applyConfig();
    }
}

bool UnixSocket::connect(const std::string& socket_path) {
    if (m_state != State::CLOSED) {
        Logger::warning("Attempted to connect on already connected Unix socket");
        return false;
    }
    
    if (!createSocket()) {
        return false;
    }
    
    updateState(State::CONNECTING);
    m_stats.connection_attempts++;
    m_socket_path = socket_path;
    
    // Setup address
    struct sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    
    if (socket_path.length() >= sizeof(addr.sun_path)) {
        Logger::error("Unix socket path too long: {}", socket_path);
        close();
        return false;
    }
    
    strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);
    
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
        Logger::debug("Connected to Unix socket: {}", socket_path);
        return true;
    }
    
    if (errno == EINPROGRESS && m_config.connect_timeout_ms > 0) {
        // Wait for connection with timeout
        struct pollfd pfd;
        pfd.fd = m_socket;
        pfd.events = POLLOUT;
        pfd.revents = 0;
        
        result = poll(&pfd, 1, static_cast<int>(m_config.connect_timeout_ms));
        
        if (result > 0 && (pfd.revents & POLLOUT)) {
            // Check if connection succeeded
            int error = 0;
            socklen_t len = sizeof(error);
            if (getsockopt(m_socket, SOL_SOCKET, SO_ERROR, &error, &len) == 0 && error == 0) {
                updateState(State::CONNECTED);
                if (!was_blocking) {
                    setNonBlocking(false);
                }
                applyConfig();
                Logger::debug("Connected to Unix socket: {}", socket_path);
                return true;
            }
        }
    }
    
    // Connection failed
    m_last_error = errno;
    m_stats.failed_operations++;
    Logger::error("Failed to connect to Unix socket {}: {}", socket_path, getLastErrorString());
    close();
    return false;
}

bool UnixSocket::bind(const std::string& socket_path) {
    if (m_state != State::CLOSED) {
        Logger::warning("Attempted to bind on non-closed Unix socket");
        return false;
    }
    
    if (!createSocket()) {
        return false;
    }
    
    m_socket_path = socket_path;
    
    // Remove existing socket file if auto-remove is enabled
    if (m_config.auto_remove_socket_file && std::filesystem::exists(socket_path)) {
        if (!removeSocketFile()) {
            Logger::warning("Failed to remove existing socket file: {}", socket_path);
        }
    }
    
    // Setup address
    struct sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    
    if (socket_path.length() >= sizeof(addr.sun_path)) {
        Logger::error("Unix socket path too long: {}", socket_path);
        close();
        return false;
    }
    
    strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);
    
    // Apply socket options before binding
    applyConfig();
    
    // Bind socket
    if (::bind(m_socket, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        m_last_error = errno;
        m_stats.failed_operations++;
        Logger::error("Failed to bind Unix socket to {}: {}", socket_path, getLastErrorString());
        close();
        return false;
    }
    
    // Set socket permissions
    if (chmod(socket_path.c_str(), m_config.socket_permissions) == -1) {
        Logger::warning("Failed to set socket permissions: {}", strerror(errno));
    }
    
    Logger::debug("Bound Unix socket to: {}", socket_path);
    return true;
}

bool UnixSocket::listen(int backlog) {
    if (m_state != State::CLOSED) {
        Logger::warning("Attempted to listen on non-closed Unix socket");
        return false;
    }
    
    if (::listen(m_socket, backlog) == -1) {
        m_last_error = errno;
        m_stats.failed_operations++;
        Logger::error("Failed to listen on Unix socket: {}", getLastErrorString());
        return false;
    }
    
    updateState(State::LISTENING);
    Logger::debug("Unix socket listening with backlog {}", backlog);
    return true;
}

UnixSocket UnixSocket::accept() {
    if (m_state != State::LISTENING) {
        Logger::warning("Attempted to accept on non-listening Unix socket");
        return UnixSocket();
    }
    
    struct sockaddr_un client_addr;
    socklen_t addr_len = sizeof(client_addr);
    
    socket_t client_socket = ::accept(m_socket, (struct sockaddr*)&client_addr, &addr_len);
    
    if (client_socket == -1) {
        m_last_error = errno;
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            m_stats.failed_operations++;
            Logger::error("Accept failed on Unix socket: {}", getLastErrorString());
        }
        return UnixSocket();
    }
    
    UnixSocket client(client_socket);
    client.setConfig(m_config);
    client.applyConfig();
    
    Logger::debug("Accepted Unix socket connection");
    return client;
}

void UnixSocket::close() {
    if (m_socket != -1) {
        ::close(m_socket);
        m_socket = -1;
    }
    
    // Remove socket file if we created it and auto-remove is enabled
    if (m_config.auto_remove_socket_file && !m_socket_path.empty() && 
        (m_state == State::LISTENING || m_state == State::CONNECTED)) {
        removeSocketFile();
    }
    
    updateState(State::CLOSED);
}

int UnixSocket::send(const void* data, size_t size) {
    if (!isValid() || !data || size == 0) {
        return -1;
    }
    
    m_stats.send_operations++;
    
    int result = ::send(m_socket, data, size, MSG_NOSIGNAL);
    
    if (result > 0) {
        m_stats.bytes_sent += result;
        m_stats.last_activity = std::chrono::steady_clock::now();
        updateStats(true, result, true);
    } else {
        m_last_error = errno;
        updateStats(true, 0, false);
        
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            Logger::debug("Send failed on Unix socket: {}", getLastErrorString());
            updateState(State::ERROR);
        }
    }
    
    return result;
}

int UnixSocket::receive(void* buffer, size_t buffer_size) {
    if (!isValid() || !buffer || buffer_size == 0) {
        return -1;
    }
    
    m_stats.receive_operations++;
    
    int result = ::recv(m_socket, buffer, buffer_size, 0);
    
    if (result > 0) {
        m_stats.bytes_received += result;
        m_stats.last_activity = std::chrono::steady_clock::now();
        updateStats(false, result, true);
    } else if (result == 0) {
        // Connection closed by peer
        Logger::debug("Unix socket connection closed by peer");
        updateState(State::CLOSED);
    } else {
        m_last_error = errno;
        updateStats(false, 0, false);
        
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            Logger::debug("Receive failed on Unix socket: {}", getLastErrorString());
            updateState(State::ERROR);
        }
    }
    
    return result;
}

bool UnixSocket::sendFileDescriptor(int fd) {
    if (!isValid() || fd < 0) {
        return false;
    }
    
    struct msghdr msg;
    struct iovec iov;
    char dummy_data = 'X';
    char control_buffer[CMSG_SPACE(sizeof(int))];
    
    // Setup message
    std::memset(&msg, 0, sizeof(msg));
    
    // Dummy data (required by some systems)
    iov.iov_base = &dummy_data;
    iov.iov_len = 1;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    
    // Control message for file descriptor
    msg.msg_control = control_buffer;
    msg.msg_controllen = sizeof(control_buffer);
    
    struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    std::memcpy(CMSG_DATA(cmsg), &fd, sizeof(int));
    
    msg.msg_controllen = CMSG_SPACE(sizeof(int));
    
    int result = sendmsg(m_socket, &msg, 0);
    if (result == -1) {
        m_last_error = errno;
        Logger::error("Failed to send file descriptor: {}", getLastErrorString());
        return false;
    }
    
    Logger::debug("Sent file descriptor {}", fd);
    return true;
}

int UnixSocket::receiveFileDescriptor() {
    if (!isValid()) {
        return -1;
    }
    
    struct msghdr msg;
    struct iovec iov;
    char dummy_data;
    char control_buffer[CMSG_SPACE(sizeof(int))];
    
    // Setup message
    std::memset(&msg, 0, sizeof(msg));
    
    // Dummy data buffer
    iov.iov_base = &dummy_data;
    iov.iov_len = 1;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    
    // Control message buffer
    msg.msg_control = control_buffer;
    msg.msg_controllen = sizeof(control_buffer);
    
    int result = recvmsg(m_socket, &msg, 0);
    if (result == -1) {
        m_last_error = errno;
        Logger::error("Failed to receive file descriptor: {}", getLastErrorString());
        return -1;
    }
    
    // Extract file descriptor from control message
    struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    if (cmsg && cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
        int received_fd;
        std::memcpy(&received_fd, CMSG_DATA(cmsg), sizeof(int));
        Logger::debug("Received file descriptor {}", received_fd);
        return received_fd;
    }
    
    Logger::warning("No file descriptor found in received message");
    return -1;
}

bool UnixSocket::sendCredentials(const Credentials& creds) {
    if (!isValid()) {
        return false;
    }
    
    struct msghdr msg;
    struct iovec iov;
    char dummy_data = 'X';
    char control_buffer[CMSG_SPACE(sizeof(struct ucred))];
    
    // Setup message
    std::memset(&msg, 0, sizeof(msg));
    
    // Dummy data
    iov.iov_base = &dummy_data;
    iov.iov_len = 1;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    
    // Control message for credentials
    msg.msg_control = control_buffer;
    msg.msg_controllen = sizeof(control_buffer);
    
    struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_CREDENTIALS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(struct ucred));
    
    struct ucred* ucred_ptr = (struct ucred*)CMSG_DATA(cmsg);
    ucred_ptr->pid = creds.pid;
    ucred_ptr->uid = creds.uid;
    ucred_ptr->gid = creds.gid;
    
    msg.msg_controllen = CMSG_SPACE(sizeof(struct ucred));
    
    int result = sendmsg(m_socket, &msg, 0);
    if (result == -1) {
        m_last_error = errno;
        Logger::error("Failed to send credentials: {}", getLastErrorString());
        return false;
    }
    
    Logger::debug("Sent credentials: pid={}, uid={}, gid={}", creds.pid, creds.uid, creds.gid);
    return true;
}

bool UnixSocket::receiveCredentials(Credentials& creds) {
    if (!isValid()) {
        return false;
    }
    
    struct msghdr msg;
    struct iovec iov;
    char dummy_data;
    char control_buffer[CMSG_SPACE(sizeof(struct ucred))];
    
    // Setup message
    std::memset(&msg, 0, sizeof(msg));
    
    // Dummy data buffer
    iov.iov_base = &dummy_data;
    iov.iov_len = 1;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    
    // Control message buffer
    msg.msg_control = control_buffer;
    msg.msg_controllen = sizeof(control_buffer);
    
    int result = recvmsg(m_socket, &msg, 0);
    if (result == -1) {
        m_last_error = errno;
        Logger::error("Failed to receive credentials: {}", getLastErrorString());
        return false;
    }
    
    // Extract credentials from control message
    struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    if (cmsg && cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_CREDENTIALS) {
        struct ucred* ucred_ptr = (struct ucred*)CMSG_DATA(cmsg);
        creds.pid = ucred_ptr->pid;
        creds.uid = ucred_ptr->uid;
        creds.gid = ucred_ptr->gid;
        
        Logger::debug("Received credentials: pid={}, uid={}, gid={}", creds.pid, creds.uid, creds.gid);
        return true;
    }
    
    Logger::warning("No credentials found in received message");
    return false;
}

bool UnixSocket::setNonBlocking(bool enabled) {
    if (!isValid()) {
        return false;
    }
    
    int flags = fcntl(m_socket, F_GETFL, 0);
    if (flags == -1) {
        m_last_error = errno;
        Logger::error("Failed to get socket flags: {}", getLastErrorString());
        return false;
    }
    
    if (enabled) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }
    
    if (fcntl(m_socket, F_SETFL, flags) == -1) {
        m_last_error = errno;
        Logger::error("Failed to set non-blocking mode: {}", getLastErrorString());
        return false;
    }
    
    return true;
}

bool UnixSocket::isDataAvailable(uint32_t timeout_ms) {
    if (!isValid()) {
        return false;
    }
    
    struct pollfd pfd;
    pfd.fd = m_socket;
    pfd.events = POLLIN;
    pfd.revents = 0;
    
    int result = poll(&pfd, 1, static_cast<int>(timeout_ms));
    return result > 0 && (pfd.revents & POLLIN);
}

bool UnixSocket::canSend(uint32_t timeout_ms) {
    if (!isValid()) {
        return false;
    }
    
    struct pollfd pfd;
    pfd.fd = m_socket;
    pfd.events = POLLOUT;
    pfd.revents = 0;
    
    int result = poll(&pfd, 1, static_cast<int>(timeout_ms));
    return result > 0 && (pfd.revents & POLLOUT);
}

bool UnixSocket::isValid() const {
    return m_socket != -1;
}

void UnixSocket::resetStats() {
    m_stats = Stats{};
    m_stats.connect_time = std::chrono::steady_clock::now();
    m_stats.last_activity = m_stats.connect_time;
}

int UnixSocket::getLastError() const {
    return m_last_error;
}

std::string UnixSocket::getLastErrorString() const {
    if (m_last_error_string.empty()) {
        m_last_error_string = std::strerror(m_last_error);
    }
    return m_last_error_string;
}

bool UnixSocket::setSendBufferSize(uint32_t size) {
    if (!isValid()) {
        return false;
    }
    
    int buf_size = static_cast<int>(size);
    return setSocketOption(SOL_SOCKET, SO_SNDBUF, &buf_size, sizeof(buf_size));
}

bool UnixSocket::setReceiveBufferSize(uint32_t size) {
    if (!isValid()) {
        return false;
    }
    
    int buf_size = static_cast<int>(size);
    return setSocketOption(SOL_SOCKET, SO_RCVBUF, &buf_size, sizeof(buf_size));
}

bool UnixSocket::setSendTimeout(uint32_t timeout_ms) {
    if (!isValid()) {
        return false;
    }
    
    struct timeval timeout;
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    return setSocketOption(SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
}

bool UnixSocket::setReceiveTimeout(uint32_t timeout_ms) {
    if (!isValid()) {
        return false;
    }
    
    struct timeval timeout;
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    return setSocketOption(SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
}

bool UnixSocket::setSocketPermissions(mode_t permissions) {
    m_config.socket_permissions = permissions;
    
    if (!m_socket_path.empty() && std::filesystem::exists(m_socket_path)) {
        if (chmod(m_socket_path.c_str(), permissions) == -1) {
            m_last_error = errno;
            Logger::error("Failed to set socket permissions: {}", getLastErrorString());
            return false;
        }
    }
    
    return true;
}

bool UnixSocket::isSupported() {
    return true;
}

// Private methods implementation

bool UnixSocket::createSocket() {
    m_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    
    if (m_socket == -1) {
        m_last_error = errno;
        Logger::error("Failed to create Unix socket: {}", getLastErrorString());
        return false;
    }
    
    return true;
}

bool UnixSocket::applyConfig() {
    if (!isValid()) {
        return false;
    }
    
    bool success = true;
    
    // Apply buffer sizes
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

void UnixSocket::updateState(State new_state) {
    if (m_state != new_state) {
        Logger::debug("Unix socket state changed: {} -> {}", 
                     static_cast<int>(m_state), static_cast<int>(new_state));
        m_state = new_state;
    }
}

void UnixSocket::updateStats(bool send_operation, size_t bytes, bool success) {
    if (!success) {
        m_stats.failed_operations++;
    }
    
    // Update activity timestamp on successful operations
    if (success && bytes > 0) {
        m_stats.last_activity = std::chrono::steady_clock::now();
    }
}

bool UnixSocket::removeSocketFile() {
    if (m_socket_path.empty()) {
        return true;
    }
    
    if (unlink(m_socket_path.c_str()) == -1) {
        if (errno != ENOENT) { // File not found is OK
            m_last_error = errno;
            Logger::error("Failed to remove socket file {}: {}", m_socket_path, getLastErrorString());
            return false;
        }
    }
    
    return true;
}

bool UnixSocket::setSocketOption(int level, int option_name, const void* option_value, socklen_t option_length) {
    if (!isValid()) {
        return false;
    }
    
    if (setsockopt(m_socket, level, option_name, option_value, option_length) == -1) {
        m_last_error = errno;
        Logger::debug("Failed to set socket option {}: {}", option_name, getLastErrorString());
        return false;
    }
    
    return true;
}

bool UnixSocket::getSocketOption(int level, int option_name, void* option_value, socklen_t* option_length) {
    if (!isValid()) {
        return false;
    }
    
    if (getsockopt(m_socket, level, option_name, option_value, option_length) == -1) {
        m_last_error = errno;
        Logger::debug("Failed to get socket option {}: {}", option_name, getLastErrorString());
        return false;
    }
    
    return true;
}

#else // Windows implementation (stubs)

UnixSocket::UnixSocket() : m_socket(-1), m_state(State::CLOSED), m_last_error(0) {
    Logger::warning("Unix sockets not supported on Windows");
}

UnixSocket::UnixSocket(socket_t existing_socket) : m_socket(-1), m_state(State::ERROR), m_last_error(0) {
    Logger::error("Unix sockets not supported on Windows");
}

UnixSocket::~UnixSocket() = default;

UnixSocket::UnixSocket(UnixSocket&& other) noexcept = default;
UnixSocket& UnixSocket::operator=(UnixSocket&& other) noexcept = default;

void UnixSocket::setConfig(const Config& config) { m_config = config; }
bool UnixSocket::connect(const std::string& socket_path) { return false; }
bool UnixSocket::bind(const std::string& socket_path) { return false; }
bool UnixSocket::listen(int backlog) { return false; }
UnixSocket UnixSocket::accept() { return UnixSocket(); }
void UnixSocket::close() {}
int UnixSocket::send(const void* data, size_t size) { return -1; }
int UnixSocket::receive(void* buffer, size_t buffer_size) { return -1; }
bool UnixSocket::sendFileDescriptor(int fd) { return false; }
int UnixSocket::receiveFileDescriptor() { return -1; }
bool UnixSocket::sendCredentials(const Credentials& creds) { return false; }
bool UnixSocket::receiveCredentials(Credentials& creds) { return false; }
bool UnixSocket::setNonBlocking(bool enabled) { return false; }
bool UnixSocket::isDataAvailable(uint32_t timeout_ms) { return false; }
bool UnixSocket::canSend(uint32_t timeout_ms) { return false; }
bool UnixSocket::isValid() const { return false; }
void UnixSocket::resetStats() {}
int UnixSocket::getLastError() const { return 0; }
std::string UnixSocket::getLastErrorString() const { return "Unix sockets not supported on Windows"; }
bool UnixSocket::setSendBufferSize(uint32_t size) { return false; }
bool UnixSocket::setReceiveBufferSize(uint32_t size) { return false; }
bool UnixSocket::setSendTimeout(uint32_t timeout_ms) { return false; }
bool UnixSocket::setReceiveTimeout(uint32_t timeout_ms) { return false; }
bool UnixSocket::setSocketPermissions(mode_t permissions) { return false; }
bool UnixSocket::isSupported() { return false; }

bool UnixSocket::createSocket() { return false; }
bool UnixSocket::applyConfig() { return false; }
void UnixSocket::updateState(State new_state) {}
void UnixSocket::updateStats(bool send_operation, size_t bytes, bool success) {}
bool UnixSocket::removeSocketFile() { return false; }
bool UnixSocket::setSocketOption(int level, int option_name, const void* option_value, socklen_t option_length) { return false; }
bool UnixSocket::getSocketOption(int level, int option_name, void* option_value, socklen_t* option_length) { return false; }

#endif // !_WIN32

// Platform-independent implementations

// UnixSocketServer implementation
UnixSocketServer::UnixSocketServer(const Config& config) : m_config(config), m_running(false) {
    m_stats.start_time = std::chrono::steady_clock::now();
}

UnixSocketServer::~UnixSocketServer() {
    if (m_running) {
        stop();
    }
}

bool UnixSocketServer::start() {
    if (!UnixSocket::isSupported()) {
        Logger::error("Unix sockets not supported on this platform");
        return false;
    }
    
    if (m_running) {
        Logger::warning("Unix socket server already running");
        return true;
    }
    
    Logger::info("Starting Unix socket server: {}", m_config.socket_path);
    
    // Configure socket
    UnixSocket::Config socket_config;
    socket_config.socket_permissions = m_config.socket_permissions;
    socket_config.auto_remove_existing = m_config.auto_remove_existing;
    
    m_listen_socket.setConfig(socket_config);
    
    if (!m_listen_socket.bind(m_config.socket_path)) {
        Logger::error("Failed to bind Unix socket server");
        return false;
    }
    
    if (!m_listen_socket.listen(m_config.listen_backlog)) {
        Logger::error("Failed to listen on Unix socket server");
        return false;
    }
    
    // Set non-blocking for timeout support
    m_listen_socket.setNonBlocking(true);
    
    m_running = true;
    m_server_thread = std::thread(&UnixSocketServer::serverLoop, this);
    
    Logger::info("Unix socket server started successfully");
    return true;
}

void UnixSocketServer::stop() {
    if (!m_running) {
        return;
    }
    
    Logger::info("Stopping Unix socket server...");
    m_running = false;
    
    if (m_server_thread.joinable()) {
        m_server_thread.join();
    }
    
    m_listen_socket.close();
    
    Logger::info("Unix socket server stopped");
}

void UnixSocketServer::setConfig(const Config& config) {
    if (m_running) {
        Logger::warning("Cannot change Unix socket server configuration while running");
        return;
    }
    m_config = config;
}

void UnixSocketServer::setClientConnectedCallback(ClientConnectedCallback callback) {
    m_client_connected_callback = callback;
}

void UnixSocketServer::setClientDisconnectedCallback(ClientDisconnectedCallback callback) {
    m_client_disconnected_callback = callback;
}

void UnixSocketServer::setErrorCallback(ErrorCallback callback) {
    m_error_callback = callback;
}

size_t UnixSocketServer::getConnectionCount() const {
    std::lock_guard<std::mutex> lock(m_stats_mutex);
    return m_stats.active_connections;
}

std::vector<std::string> UnixSocketServer::getConnectedClients() const {
    // Unix sockets don't have traditional addresses, return client info
    return {};
}

bool UnixSocketServer::disconnectClient(const std::string& client_info) {
    Logger::info("Disconnect request for Unix socket client: {}", client_info);
    return false;
}

void UnixSocketServer::resetStats() {
    std::lock_guard<std::mutex> lock(m_stats_mutex);
    m_stats = Stats{};
    m_stats.start_time = std::chrono::steady_clock::now();
}

void UnixSocketServer::serverLoop() {
    Logger::debug("Unix socket server loop started");
    
    while (m_running) {
        try {
            handleNewConnection();
            std::this_thread::sleep_for(std::chrono::milliseconds(m_config.accept_timeout_ms));
        } catch (const std::exception& e) {
            Logger::error("Exception in Unix socket server loop: {}", e.what());
            if (m_error_callback) {
                m_error_callback("Server loop exception: " + std::string(e.what()));
            }
        }
    }
    
    Logger::debug("Unix socket server loop ended");
}

void UnixSocketServer::handleNewConnection() {
    if (!m_listen_socket.isDataAvailable(0)) {
        return;
    }
    
    UnixSocket client_socket = m_listen_socket.accept();
    if (!client_socket.isValid()) {
        return;
    }
    
    {
        std::lock_guard<std::mutex> lock(m_stats_mutex);
        m_stats.total_connections++;
        m_stats.active_connections++;
    }
    
    if (m_client_connected_callback) {
        m_client_connected_callback(std::move(client_socket), "Unix socket client");
    }
    
    Logger::debug("New Unix socket client connected");
}

// UnixSocketClient implementation

UnixSocketClient::UnixSocketClient() = default;

UnixSocketClient::~UnixSocketClient() {
    disconnect();
}

bool UnixSocketClient::connect(const std::string& socket_path) {
    disconnect();
    
    if (m_socket.connect(socket_path)) {
        if (m_connected_callback) {
            m_connected_callback();
        }
        return true;
    }
    
    return false;
}

void UnixSocketClient::disconnect() {
    if (m_socket.isConnected()) {
        if (m_disconnected_callback) {
            m_disconnected_callback("Client disconnect");
        }
    }
    
    m_socket.close();
}

bool UnixSocketClient::isConnected() const {
    return m_socket.isConnected();
}

bool UnixSocketClient::send(const std::vector<uint8_t>& data) {
    return send(data.data(), data.size());
}

bool UnixSocketClient::send(const void* data, size_t size) {
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

std::vector<uint8_t> UnixSocketClient::receive(size_t max_size) {
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

bool UnixSocketClient::sendFileDescriptor(int fd) {
    return m_socket.sendFileDescriptor(fd);
}

int UnixSocketClient::receiveFileDescriptor() {
    return m_socket.receiveFileDescriptor();
}

void UnixSocketClient::setConnectedCallback(ConnectedCallback callback) {
    m_connected_callback = callback;
}

void UnixSocketClient::setDisconnectedCallback(DisconnectedCallback callback) {
    m_disconnected_callback = callback;
}

void UnixSocketClient::setDataReceivedCallback(DataReceivedCallback callback) {
    m_data_received_callback = callback;
}

void UnixSocketClient::setErrorCallback(ErrorCallback callback) {
    m_error_callback = callback;
}

void UnixSocketClient::setConfig(const UnixSocket::Config& config) {
    m_socket.setConfig(config);
}

const UnixSocket::Stats& UnixSocketClient::getStats() const {
    return m_socket.getStats();
}

// UnixSocketUtils implementation

namespace UnixSocketUtils {

bool isValidSocketPath(const std::string& path) {
    if (path.empty() || path.length() >= 108) { // UNIX_PATH_MAX
        return false;
    }
    
    // Check for invalid characters (simplified)
    return path.find('\0') == std::string::npos;
}

bool socketExists(const std::string& path) {
    return std::filesystem::exists(path);
}

bool removeSocketFile(const std::string& path) {
    if (!socketExists(path)) {
        return true;
    }
    
    return std::filesystem::remove(path);
}

bool setSocketPermissions(const std::string& path, mode_t permissions) {
#ifndef _WIN32
    return chmod(path.c_str(), permissions) == 0;
#else
    return false;
#endif
}

mode_t getSocketPermissions(const std::string& path) {
#ifndef _WIN32
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        return st.st_mode & 0777;
    }
#endif
    return 0;
}

bool getProcessInfo(const UnixSocket& socket, ProcessInfo& info) {
#ifndef _WIN32
    UnixSocket::Credentials creds;
    UnixSocket& non_const_socket = const_cast<UnixSocket&>(socket);
    
    if (non_const_socket.receiveCredentials(creds)) {
        info.pid = creds.pid;
        info.uid = creds.uid;
        info.gid = creds.gid;
        
        // Get process name
        char proc_path[256];
        snprintf(proc_path, sizeof(proc_path), "/proc/%d/comm", info.pid);
        
        std::ifstream comm_file(proc_path);
        if (comm_file.is_open()) {
            std::getline(comm_file, info.process_name);
        }
        
        // Get user name
        struct passwd* pwd = getpwuid(info.uid);
        if (pwd) {
            info.user_name = pwd->pw_name;
        }
        
        // Get group name
        struct group* grp = getgrgid(info.gid);
        if (grp) {
            info.group_name = grp->gr_name;
        }
        
        return true;
    }
#endif
    
    return false;
}

bool getCurrentProcessInfo(ProcessInfo& info) {
#ifndef _WIN32
    info.pid = getpid();
    info.uid = getuid();
    info.gid = getgid();
    
    // Get current process name
    std::ifstream comm_file("/proc/self/comm");
    if (comm_file.is_open()) {
        std::getline(comm_file, info.process_name);
    }
    
    // Get current user name
    struct passwd* pwd = getpwuid(info.uid);
    if (pwd) {
        info.user_name = pwd->pw_name;
    }
    
    // Get current group name
    struct group* grp = getgrgid(info.gid);
    if (grp) {
        info.group_name = grp->gr_name;
    }
    
    return true;
#else
    return false;
#endif
}

std::pair<UnixSocket, UnixSocket> createSocketPair() {
    UnixSocket socket1, socket2;
    
#ifndef _WIN32
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0) {
        socket1 = UnixSocket(sv[0]);
        socket2 = UnixSocket(sv[1]);
    }
#endif
    
    return {std::move(socket1), std::move(socket2)};
}

std::string getDefaultSocketPath(const std::string& app_name) {
    return "/tmp/" + app_name + ".sock";
}

std::string getTempSocketPath(const std::string& prefix) {
    return "/tmp/" + prefix + "_" + std::to_string(getpid()) + ".sock";
}

bool createSocketDirectory(const std::string& socket_path) {
    std::filesystem::path path(socket_path);
    std::filesystem::path dir = path.parent_path();
    
    if (dir.empty()) {
        return true;
    }
    
    try {
        return std::filesystem::create_directories(dir);
    } catch (const std::filesystem::filesystem_error& e) {
        Logger::error("Failed to create socket directory {}: {}", dir.string(), e.what());
        return false;
    }
}

bool testUnixSocketConnection(const std::string& socket_path, uint32_t timeout_ms) {
    if (!UnixSocket::isSupported()) {
        return false;
    }
    
    UnixSocket socket;
    UnixSocket::Config config;
    config.connect_timeout_ms = timeout_ms;
    socket.setConfig(config);
    
    return socket.connect(socket_path);
}

bool isUnixSocketSupported() {
    return UnixSocket::isSupported();
}

std::string getUnsupportedReason() {
#ifdef _WIN32
    return "Unix domain sockets are not supported on Windows";
#else
    return "";
#endif
}

} // namespace UnixSocketUtils

} // namespace Kairos