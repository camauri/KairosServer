// KairosServer/src/Network/Client.cpp
#include <Network/Client.hpp>
#include <Utils/Logger.hpp>
#include <Protocol.hpp>
#include <cstring>
#include <algorithm>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <errno.h>
#endif

namespace Kairos {

// Factory methods
std::shared_ptr<Client> Client::createTcp(socket_t socket, const std::string& address, uint16_t port) {
    auto client = std::shared_ptr<Client>(new Client(socket, Type::TCP));
    client->m_info.endpoint_address = address;
    client->m_info.endpoint_port = port;
    client->m_info.connection_type = Type::TCP;
    return client;
}

std::shared_ptr<Client> Client::createUnix(socket_t socket, const std::string& path) {
    auto client = std::shared_ptr<Client>(new Client(socket, Type::UNIX_SOCKET));
    client->m_info.endpoint_address = path;
    client->m_info.endpoint_port = 0;
    client->m_info.connection_type = Type::UNIX_SOCKET;
    return client;
}

Client::Client(socket_t socket, Type type) : m_socket(socket) {
    m_info.connection_type = type;
    m_info.connect_time = std::chrono::steady_clock::now();
    m_info.last_activity = m_info.connect_time;
    
    m_receive_buffer.reserve(64 * 1024); // Default 64KB
    m_send_buffer.reserve(64 * 1024);
}

Client::~Client() {
    if (m_socket != -1) {
        disconnect("Client destroyed");
    }
}

bool Client::initialize(uint32_t client_id, const Config& config) {
    m_info.client_id = client_id;
    m_config = config;
    
    // Resize buffers
    m_receive_buffer.reserve(m_config.receive_buffer_size);
    m_send_buffer.reserve(m_config.send_buffer_size);
    
    // Configure socket
    if (!configureSocket()) {
        Logger::error("Failed to configure socket for client {}", client_id);
        return false;
    }
    
    setState(State::HANDSHAKE);
    
    Logger::debug("Client {} initialized", client_id);
    return true;
}

void Client::disconnect(const std::string& reason) {
    if (m_state.load() == State::DISCONNECTED) {
        return;
    }
    
    Logger::info("Disconnecting client {} ({})", m_info.client_id, reason);
    m_disconnect_reason = reason;
    setState(State::DISCONNECTING);
    
    if (m_socket != -1) {
#ifdef _WIN32
        closesocket(m_socket);
#else
        close(m_socket);
#endif
        m_socket = -1;
    }
    
    setState(State::DISCONNECTED);
}

bool Client::isConnected() const {
    State state = m_state.load();
    return state == State::CONNECTED || state == State::HANDSHAKE;
}

bool Client::isTimedOut() const {
    if (!isConnected()) {
        return false;
    }
    
    auto now = std::chrono::steady_clock::now();
    auto idle_time = std::chrono::duration_cast<std::chrono::seconds>(
        now - m_info.last_activity);
    
    return idle_time.count() > m_config.timeout_seconds;
}

bool Client::sendMessage(const MessageHeader& header, const void* data) {
    if (!isConnected()) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(m_send_mutex);
    
    // Validate message
    if (!validateMessageHeader(header)) {
        Logger::warning("Invalid message header from client {}", m_info.client_id);
        return false;
    }
    
    // Prepare message buffer
    size_t message_size = sizeof(MessageHeader) + header.data_size;
    if (!ensureSendBufferSpace(message_size)) {
        Logger::error("Send buffer overflow for client {}", m_info.client_id);
        return false;
    }
    
    // Copy header with network byte order
    MessageHeader net_header = header;
    ProtocolHelper::hostToNetwork(net_header);
    
    // Append to send buffer
    size_t old_size = m_send_buffer.size();
    m_send_buffer.resize(old_size + message_size);
    
    std::memcpy(m_send_buffer.data() + old_size, &net_header, sizeof(MessageHeader));
    if (data && header.data_size > 0) {
        std::memcpy(m_send_buffer.data() + old_size + sizeof(MessageHeader), data, header.data_size);
    }
    
    // Send data
    bool success = sendRawData(m_send_buffer.data() + m_send_buffer_pos, 
                              m_send_buffer.size() - m_send_buffer_pos);
    
    if (success) {
        m_info.messages_sent++;
        m_info.bytes_sent += message_size;
        updateActivity();
        
        // Clear sent data from buffer
        m_send_buffer.clear();
        m_send_buffer_pos = 0;
    }
    
    return success;
}

bool Client::receiveMessages(std::vector<std::pair<MessageHeader, std::vector<uint8_t>>>& messages) {
    if (!isConnected()) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(m_receive_mutex);
    
    // Receive new data
    if (!receiveRawData()) {
        return false;
    }
    
    // Parse messages from buffer
    if (!parseMessages()) {
        return false;
    }
    
    // Return parsed messages
    messages.clear();
    while (!m_parsed_messages.empty()) {
        messages.push_back(std::move(m_parsed_messages.front()));
        m_parsed_messages.pop();
    }
    
    if (!messages.empty()) {
        m_info.messages_received += messages.size();
        updateActivity();
    }
    
    return true;
}

bool Client::performHandshake(const ServerHello& server_hello) {
    if (m_state.load() != State::HANDSHAKE) {
        return false;
    }
    
    // Store server information
    m_info.client_version = server_hello.server_version;
    
    setState(State::CONNECTED);
    Logger::debug("Handshake completed for client {}", m_info.client_id);
    
    return true;
}

void Client::sendPing() {
    if (!isConnected()) {
        return;
    }
    
    PingData ping_data;
    ping_data.client_timestamp = ProtocolHelper::getCurrentTimestamp();
    
    MessageHeader header = ProtocolHelper::createHeader(MessageType::PING, m_info.client_id, 
                                                       m_info.ping_sequence++, sizeof(PingData));
    
    if (sendMessage(header, &ping_data)) {
        m_last_ping_sent = std::chrono::steady_clock::now();
        Logger::debug("Sent ping to client {}", m_info.client_id);
    }
}

void Client::handlePong(const PongData& pong) {
    auto now = std::chrono::steady_clock::now();
    m_last_pong_received = now;
    
    // Calculate latency
    uint64_t current_time = ProtocolHelper::getCurrentTimestamp();
    if (current_time > pong.client_timestamp) {
        double latency_ms = (current_time - pong.client_timestamp) / 1000.0;
        updateLatency(latency_ms);
        
        Logger::debug("Received pong from client {}, latency: {:.2f}ms", 
                     m_info.client_id, latency_ms);
    }
}

bool Client::needsPing() const {
    if (!m_config.enable_keep_alive) {
        return false;
    }
    
    auto now = std::chrono::steady_clock::now();
    auto time_since_ping = std::chrono::duration_cast<std::chrono::seconds>(
        now - m_last_ping_sent);
    
    return time_since_ping.count() >= m_config.ping_interval_seconds;
}

bool Client::checkRateLimit() {
    auto now = std::chrono::steady_clock::now();
    
    // Remove old timestamps
    while (!m_message_times.empty() && 
           now - m_message_times.front() > std::chrono::seconds(1)) {
        m_message_times.pop();
    }
    
    // Check rate limit
    if (m_message_times.size() >= MAX_MESSAGES_PER_SECOND) {
        m_info.errors++;
        return false;
    }
    
    m_message_times.push(now);
    return true;
}

void Client::updateActivity() {
    m_info.last_activity = std::chrono::steady_clock::now();
}

void Client::updateLatency(double latency_ms) {
    // Simple exponential moving average
    if (m_info.avg_latency_ms == 0.0) {
        m_info.avg_latency_ms = latency_ms;
    } else {
        m_info.avg_latency_ms = m_info.avg_latency_ms * 0.9 + latency_ms * 0.1;
    }
}

std::string Client::getStatusString() const {
    std::ostringstream ss;
    ss << "Client " << m_info.client_id << " (" << getEndpointString() << "): ";
    
    switch (m_state.load()) {
        case State::CONNECTING: ss << "CONNECTING"; break;
        case State::HANDSHAKE: ss << "HANDSHAKE"; break;
        case State::CONNECTED: ss << "CONNECTED"; break;
        case State::DISCONNECTING: ss << "DISCONNECTING"; break;
        case State::DISCONNECTED: ss << "DISCONNECTED"; break;
        case State::ERROR: ss << "ERROR"; break;
    }
    
    if (isConnected()) {
        ss << ", msgs=" << m_info.messages_received << "/" << m_info.messages_sent;
        ss << ", latency=" << std::fixed << std::setprecision(1) << m_info.avg_latency_ms << "ms";
        
        auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - m_info.connect_time);
        ss << ", uptime=" << uptime.count() << "s";
    }
    
    if (!m_disconnect_reason.empty()) {
        ss << " (" << m_disconnect_reason << ")";
    }
    
    return ss.str();
}

// Private methods implementation

bool Client::sendRawData(const void* data, size_t size) {
    if (m_socket == -1 || size == 0) {
        return false;
    }
    
    const uint8_t* buffer = static_cast<const uint8_t*>(data);
    size_t bytes_sent = 0;
    
    while (bytes_sent < size) {
        ssize_t result = send(m_socket, 
                             reinterpret_cast<const char*>(buffer + bytes_sent), 
                             size - bytes_sent, 
                             MSG_NOSIGNAL);
        
        if (result <= 0) {
            if (result == 0) {
                Logger::debug("Client {} connection closed by peer", m_info.client_id);
                setState(State::DISCONNECTED);
                return false;
            }
            
#ifdef _WIN32
            int error = WSAGetLastError();
            if (error == WSAEWOULDBLOCK) {
#else
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
#endif
                // Would block, try again later
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            
            Logger::error("Send failed for client {}: {}", m_info.client_id, strerror(errno));
            setState(State::ERROR);
            return false;
        }
        
        bytes_sent += result;
    }
    
    return true;
}

bool Client::receiveRawData() {
    if (m_socket == -1) {
        return false;
    }
    
    // Ensure buffer has space
    if (!ensureReceiveBufferSpace(1024)) {
        return false;
    }
    
    // Receive data
    size_t available_space = m_receive_buffer.capacity() - m_receive_buffer.size();
    m_receive_buffer.resize(m_receive_buffer.size() + available_space);
    
    ssize_t result = recv(m_socket, 
                         reinterpret_cast<char*>(m_receive_buffer.data() + m_receive_buffer_pos), 
                         available_space, 
                         MSG_DONTWAIT);
    
    if (result <= 0) {
        m_receive_buffer.resize(m_receive_buffer.size() - available_space);
        
        if (result == 0) {
            Logger::debug("Client {} connection closed by peer", m_info.client_id);
            setState(State::DISCONNECTED);
            return false;
        }
        
#ifdef _WIN32
        int error = WSAGetLastError();
        if (error == WSAEWOULDBLOCK) {
#else
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
#endif
            // No data available, not an error
            return true;
        }
        
        Logger::error("Receive failed for client {}: {}", m_info.client_id, strerror(errno));
        setState(State::ERROR);
        return false;
    }
    
    // Adjust buffer size
    m_receive_buffer.resize(m_receive_buffer.size() - available_space + result);
    m_info.bytes_received += result;
    
    return true;
}

bool Client::parseMessages() {
    while (m_receive_buffer.size() - m_receive_buffer_pos >= sizeof(MessageHeader)) {
        // Parse header
        MessageHeader header;
        std::memcpy(&header, m_receive_buffer.data() + m_receive_buffer_pos, sizeof(MessageHeader));
        ProtocolHelper::networkToHost(header);
        
        // Validate header
        if (!validateMessageHeader(header)) {
            Logger::error("Invalid message header from client {}", m_info.client_id);
            setState(State::ERROR);
            return false;
        }
        
        // Check if we have the complete message
        size_t message_size = sizeof(MessageHeader) + header.data_size;
        if (m_receive_buffer.size() - m_receive_buffer_pos < message_size) {
            // Incomplete message, wait for more data
            break;
        }
        
        // Extract message data
        std::vector<uint8_t> data;
        if (header.data_size > 0) {
            data.resize(header.data_size);
            std::memcpy(data.data(), 
                       m_receive_buffer.data() + m_receive_buffer_pos + sizeof(MessageHeader), 
                       header.data_size);
        }
        
        // Add to parsed messages queue
        m_parsed_messages.emplace(header, std::move(data));
        
        // Advance buffer position
        m_receive_buffer_pos += message_size;
    }
    
    // Compact buffer if needed
    if (m_receive_buffer_pos > m_receive_buffer.size() / 2) {
        compactReceiveBuffer();
    }
    
    return true;
}

bool Client::ensureReceiveBufferSpace(size_t needed_space) {
    size_t available_space = m_receive_buffer.capacity() - m_receive_buffer.size();
    
    if (available_space < needed_space) {
        // Try compacting first
        compactReceiveBuffer();
        available_space = m_receive_buffer.capacity() - m_receive_buffer.size();
        
        if (available_space < needed_space) {
            // Resize buffer if within limits
            size_t new_capacity = m_receive_buffer.capacity() + needed_space;
            if (new_capacity > m_config.receive_buffer_size * 2) {
                Logger::error("Receive buffer overflow for client {}", m_info.client_id);
                return false;
            }
            
            m_receive_buffer.reserve(new_capacity);
        }
    }
    
    return true;
}

bool Client::ensureSendBufferSpace(size_t needed_space) {
    if (m_send_buffer.size() + needed_space > m_config.send_buffer_size * 2) {
        Logger::error("Send buffer overflow for client {}", m_info.client_id);
        return false;
    }
    
    if (m_send_buffer.capacity() < m_send_buffer.size() + needed_space) {
        m_send_buffer.reserve(m_send_buffer.size() + needed_space);
    }
    
    return true;
}

void Client::compactReceiveBuffer() {
    if (m_receive_buffer_pos > 0) {
        size_t remaining_data = m_receive_buffer.size() - m_receive_buffer_pos;
        if (remaining_data > 0) {
            std::memmove(m_receive_buffer.data(), 
                        m_receive_buffer.data() + m_receive_buffer_pos, 
                        remaining_data);
        }
        
        m_receive_buffer.resize(remaining_data);
        m_receive_buffer_pos = 0;
    }
}

bool Client::configureSocket() {
    if (m_socket == -1) {
        return false;
    }
    
    // Set non-blocking mode
    if (!setSocketNonBlocking()) {
        return false;
    }
    
    // Set socket options
    if (!setSocketOptions()) {
        return false;
    }
    
    return true;
}

bool Client::setSocketNonBlocking() {
#ifdef _WIN32
    u_long mode = 1;
    if (ioctlsocket(m_socket, FIONBIO, &mode) != 0) {
        Logger::error("Failed to set non-blocking mode: {}", WSAGetLastError());
        return false;
    }
#else
    int flags = fcntl(m_socket, F_GETFL, 0);
    if (flags == -1) {
        Logger::error("Failed to get socket flags: {}", strerror(errno));
        return false;
    }
    
    if (fcntl(m_socket, F_SETFL, flags | O_NONBLOCK) == -1) {
        Logger::error("Failed to set non-blocking mode: {}", strerror(errno));
        return false;
    }
#endif
    
    return true;
}

bool Client::setSocketOptions() {
    // Disable Nagle algorithm for low latency
    if (!m_config.enable_nagle) {
        int opt = 1;
        if (setsockopt(m_socket, IPPROTO_TCP, TCP_NODELAY, 
                      reinterpret_cast<const char*>(&opt), sizeof(opt)) < 0) {
            Logger::warning("Failed to set TCP_NODELAY for client {}: {}", 
                           m_info.client_id, strerror(errno));
        }
    }
    
    // Enable keep-alive
    if (m_config.enable_keep_alive) {
        int opt = 1;
        if (setsockopt(m_socket, SOL_SOCKET, SO_KEEPALIVE, 
                      reinterpret_cast<const char*>(&opt), sizeof(opt)) < 0) {
            Logger::warning("Failed to set SO_KEEPALIVE for client {}: {}", 
                           m_info.client_id, strerror(errno));
        }
    }
    
    return true;
}

bool Client::validateMessageHeader(const MessageHeader& header) const {
    return ProtocolHelper::validateHeader(header) && 
           validateMessageSize(header.data_size);
}

bool Client::validateMessageSize(size_t size) const {
    return size <= m_config.max_message_size;
}

void Client::setState(State new_state) {
    State old_state = m_state.exchange(new_state);
    
    if (old_state != new_state) {
        Logger::debug("Client {} state changed: {} -> {}", 
                     m_info.client_id, 
                     static_cast<int>(old_state), 
                     static_cast<int>(new_state));
    }
}

void Client::logError(const std::string& message) {
    Logger::error("Client {}: {}", m_info.client_id, message);
    m_info.errors++;
    m_consecutive_errors++;
    
    if (m_consecutive_errors >= MAX_CONSECUTIVE_ERRORS) {
        Logger::error("Too many consecutive errors for client {}, disconnecting", m_info.client_id);
        setState(State::ERROR);
    }
}

std::string Client::getEndpointString() const {
    if (m_info.connection_type == Type::TCP) {
        return m_info.endpoint_address + ":" + std::to_string(m_info.endpoint_port);
    } else {
        return m_info.endpoint_address;
    }
}

} // namespace Kairos