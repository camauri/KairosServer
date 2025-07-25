// shared/src/Protocol.cpp
#include <Protocol.hpp>
#include <Types.hpp>
#include <Constants.hpp>
#include <chrono>
#include <cstring>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <arpa/inet.h>
    #include <endian.h>
#endif

namespace Kairos {

// ProtocolHelper implementation

bool ProtocolHelper::validateHeader(const MessageHeader& header) {
    // Check magic number
    if (header.magic != 0x4B41524F) { // "KARO"
        return false;
    }
    
    // Check protocol version
    if (header.protocol_version != PROTOCOL_VERSION) {
        return false;
    }
    
    // Check message type
    if (static_cast<uint8_t>(header.type) > static_cast<uint8_t>(MessageType::DISCONNECT)) {
        return false;
    }
    
    // Check data size reasonableness (max 10MB)
    if (header.data_size > 10 * 1024 * 1024) {
        return false;
    }
    
    return true;
}

size_t ProtocolHelper::getMessageSize(const MessageHeader& header) {
    return sizeof(MessageHeader) + header.data_size;
}

uint64_t ProtocolHelper::getCurrentTimestamp() {
    auto now = std::chrono::steady_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
}

MessageHeader ProtocolHelper::createHeader(MessageType type, uint32_t client_id, 
                                          uint32_t sequence, uint32_t data_size, 
                                          uint8_t layer_id) {
    MessageHeader header;
    header.magic = 0x4B41524F; // "KARO"
    header.protocol_version = PROTOCOL_VERSION;
    header.type = type;
    header.layer_id = layer_id;
    header.reserved = 0;
    header.client_id = client_id;
    header.sequence = sequence;
    header.data_size = data_size;
    header.timestamp = getCurrentTimestamp();
    
    return header;
}

std::vector<uint8_t> ProtocolHelper::createMessage(const MessageHeader& header, const void* data) {
    std::vector<uint8_t> message;
    message.resize(sizeof(MessageHeader) + header.data_size);
    
    // Copy header
    MessageHeader network_header = header;
    hostToNetwork(network_header);
    std::memcpy(message.data(), &network_header, sizeof(MessageHeader));
    
    // Copy data if present
    if (data && header.data_size > 0) {
        std::memcpy(message.data() + sizeof(MessageHeader), data, header.data_size);
    }
    
    return message;
}

void ProtocolHelper::hostToNetwork(MessageHeader& header) {
    header.magic = htonl(header.magic);
    header.protocol_version = htonl(header.protocol_version);
    // type and layer_id are single bytes, no conversion needed
    header.reserved = htons(header.reserved);
    header.client_id = htonl(header.client_id);
    header.sequence = htonl(header.sequence);
    header.data_size = htonl(header.data_size);
    
#ifdef _WIN32
    // Windows doesn't have htobe64, use manual conversion
    header.timestamp = ((uint64_t)htonl((uint32_t)header.timestamp) << 32) | htonl((uint32_t)(header.timestamp >> 32));
#else
    header.timestamp = htobe64(header.timestamp);
#endif
}

void ProtocolHelper::networkToHost(MessageHeader& header) {
    header.magic = ntohl(header.magic);
    header.protocol_version = ntohl(header.protocol_version);
    // type and layer_id are single bytes, no conversion needed
    header.reserved = ntohs(header.reserved);
    header.client_id = ntohl(header.client_id);
    header.sequence = ntohl(header.sequence);
    header.data_size = ntohl(header.data_size);
    
#ifdef _WIN32
    // Windows doesn't have be64toh, use manual conversion
    header.timestamp = ((uint64_t)ntohl((uint32_t)header.timestamp) << 32) | ntohl((uint32_t)(header.timestamp >> 32));
#else
    header.timestamp = be64toh(header.timestamp);
#endif
}

ServerHello ProtocolHelper::createServerHello(uint32_t client_id, uint32_t server_version) {
    ServerHello hello;
    hello.server_version = server_version;
    hello.max_clients = 32;
    hello.assigned_client_id = client_id;
    hello.server_capabilities = 
        Capabilities::BASIC_RENDERING |
        Capabilities::TEXT_RENDERING |
        Capabilities::TEXTURED_RENDERING |
        Capabilities::LAYER_SUPPORT |
        Capabilities::INPUT_EVENTS |
        Capabilities::FRAME_CALLBACKS |
        Capabilities::UNIX_SOCKETS;
    hello.max_layers = 255;
    
    return hello;
}

ErrorResponse ProtocolHelper::createErrorResponse(ErrorCode error_code, const std::string& message, 
                                                 uint32_t original_sequence) {
    ErrorResponse response;
    response.error_code = error_code;
    response.original_sequence = original_sequence;
    
    // Copy message, ensuring null termination
    size_t copy_len = std::min(message.length(), sizeof(response.error_message) - 1);
    std::memcpy(response.error_message, message.c_str(), copy_len);
    response.error_message[copy_len] = '\0';
    
    return response;
}

PongData ProtocolHelper::createPongResponse(const PingData& ping_data, uint32_t server_load, 
                                           uint32_t queue_depth) {
    PongData pong;
    pong.client_timestamp = ping_data.client_timestamp;
    pong.server_timestamp = getCurrentTimestamp();
    pong.server_load = server_load;
    pong.queue_depth = queue_depth;
    
    return pong;
}

// Template specializations for common message types
template<>
std::vector<uint8_t> ProtocolHelper::serialize<ClientHello>(const MessageHeader& header, const ClientHello& data) {
    auto message = createMessage(header, &data);
    return message;
}

template<>
std::vector<uint8_t> ProtocolHelper::serialize<ServerHello>(const MessageHeader& header, const ServerHello& data) {
    auto message = createMessage(header, &data);
    return message;
}

template<>
std::vector<uint8_t> ProtocolHelper::serialize<DrawPointData>(const MessageHeader& header, const DrawPointData& data) {
    auto message = createMessage(header, &data);
    return message;
}

template<>
std::vector<uint8_t> ProtocolHelper::serialize<DrawLineData>(const MessageHeader& header, const DrawLineData& data) {
    auto message = createMessage(header, &data);
    return message;
}

template<>
std::vector<uint8_t> ProtocolHelper::serialize<DrawRectangleData>(const MessageHeader& header, const DrawRectangleData& data) {
    auto message = createMessage(header, &data);
    return message;
}

template<>
std::vector<uint8_t> ProtocolHelper::serialize<ErrorResponse>(const MessageHeader& header, const ErrorResponse& data) {
    auto message = createMessage(header, &data);
    return message;
}

template<>
bool ProtocolHelper::deserialize<ClientHello>(const std::vector<uint8_t>& buffer, MessageHeader& header, ClientHello& data) {
    if (buffer.size() < sizeof(MessageHeader) + sizeof(ClientHello)) {
        return false;
    }
    
    // Extract header
    std::memcpy(&header, buffer.data(), sizeof(MessageHeader));
    networkToHost(header);
    
    if (!validateHeader(header) || header.data_size != sizeof(ClientHello)) {
        return false;
    }
    
    // Extract data
    std::memcpy(&data, buffer.data() + sizeof(MessageHeader), sizeof(ClientHello));
    
    return true;
}

template<>
bool ProtocolHelper::deserialize<ServerHello>(const std::vector<uint8_t>& buffer, MessageHeader& header, ServerHello& data) {
    if (buffer.size() < sizeof(MessageHeader) + sizeof(ServerHello)) {
        return false;
    }
    
    // Extract header
    std::memcpy(&header, buffer.data(), sizeof(MessageHeader));
    networkToHost(header);
    
    if (!validateHeader(header) || header.data_size != sizeof(ServerHello)) {
        return false;
    }
    
    // Extract data
    std::memcpy(&data, buffer.data() + sizeof(MessageHeader), sizeof(ServerHello));
    
    return true;
}

} // namespace Kairos