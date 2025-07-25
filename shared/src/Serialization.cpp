// shared/src/Serialization.cpp
#include <Protocol.hpp>
#include <Types.hpp>
#include <Constants.hpp>
#include <cstring>

namespace Kairos {

// Color constants implementation
const Color Color::WHITE{255, 255, 255, 255};
const Color Color::BLACK{0, 0, 0, 255};
const Color Color::RED{255, 0, 0, 255};
const Color Color::GREEN{0, 255, 0, 255};
const Color Color::BLUE{0, 0, 255, 255};
const Color Color::TRANSPARENT{0, 0, 0, 0};

// Helper functions for message creation
std::vector<uint8_t> createDrawTextMessage(uint32_t client_id, uint32_t sequence, uint8_t layer_id,
                                          const DrawTextData& text_data, const std::string& text) {
    MessageHeader header = ProtocolHelper::createHeader(MessageType::DRAW_TEXT, client_id, sequence, 
                                                       sizeof(DrawTextData) + text.length(), layer_id);
    
    std::vector<uint8_t> message;
    message.resize(sizeof(MessageHeader) + sizeof(DrawTextData) + text.length());
    
    // Copy header
    ProtocolHelper::hostToNetwork(header);
    std::memcpy(message.data(), &header, sizeof(MessageHeader));
    
    // Copy text data
    std::memcpy(message.data() + sizeof(MessageHeader), &text_data, sizeof(DrawTextData));
    
    // Copy text string
    std::memcpy(message.data() + sizeof(MessageHeader) + sizeof(DrawTextData), 
                text.c_str(), text.length());
    
    return message;
}

std::vector<uint8_t> createDrawPolygonMessage(uint32_t client_id, uint32_t sequence, uint8_t layer_id,
                                             const DrawPolygonData& polygon_data, 
                                             const std::vector<Point>& points) {
    size_t points_size = points.size() * sizeof(Point);
    MessageHeader header = ProtocolHelper::createHeader(MessageType::DRAW_POLYGON, client_id, sequence, 
                                                       sizeof(DrawPolygonData) + points_size, layer_id);
    
    std::vector<uint8_t> message;
    message.resize(sizeof(MessageHeader) + sizeof(DrawPolygonData) + points_size);
    
    // Copy header
    ProtocolHelper::hostToNetwork(header);
    std::memcpy(message.data(), &header, sizeof(MessageHeader));
    
    // Copy polygon data
    std::memcpy(message.data() + sizeof(MessageHeader), &polygon_data, sizeof(DrawPolygonData));
    
    // Copy points
    std::memcpy(message.data() + sizeof(MessageHeader) + sizeof(DrawPolygonData), 
                points.data(), points_size);
    
    return message;
}

std::vector<uint8_t> createDrawTexturedQuadsMessage(uint32_t client_id, uint32_t sequence, uint8_t layer_id,
                                                   const DrawTexturedQuadsData& quad_data,
                                                   const std::vector<TexturedVertex>& vertices) {
    size_t vertices_size = vertices.size() * sizeof(TexturedVertex);
    MessageHeader header = ProtocolHelper::createHeader(MessageType::DRAW_TEXTURED_QUADS, client_id, sequence, 
                                                       sizeof(DrawTexturedQuadsData) + vertices_size, layer_id);
    
    std::vector<uint8_t> message;
    message.resize(sizeof(MessageHeader) + sizeof(DrawTexturedQuadsData) + vertices_size);
    
    // Copy header
    ProtocolHelper::hostToNetwork(header);
    std::memcpy(message.data(), &header, sizeof(MessageHeader));
    
    // Copy quad data
    std::memcpy(message.data() + sizeof(MessageHeader), &quad_data, sizeof(DrawTexturedQuadsData));
    
    // Copy vertices
    std::memcpy(message.data() + sizeof(MessageHeader) + sizeof(DrawTexturedQuadsData), 
                vertices.data(), vertices_size);
    
    return message;
}

std::vector<uint8_t> createFontTextureMessage(uint32_t client_id, uint32_t sequence, uint8_t layer_id,
                                             const FontTextureData& texture_data,
                                             const void* pixel_data) {
    MessageHeader header = ProtocolHelper::createHeader(MessageType::UPLOAD_FONT_TEXTURE, client_id, sequence, 
                                                       sizeof(FontTextureData) + texture_data.data_size, layer_id);
    
    std::vector<uint8_t> message;
    message.resize(sizeof(MessageHeader) + sizeof(FontTextureData) + texture_data.data_size);
    
    // Copy header
    ProtocolHelper::hostToNetwork(header);
    std::memcpy(message.data(), &header, sizeof(MessageHeader));
    
    // Copy texture data
    std::memcpy(message.data() + sizeof(MessageHeader), &texture_data, sizeof(FontTextureData));
    
    // Copy pixel data
    std::memcpy(message.data() + sizeof(MessageHeader) + sizeof(FontTextureData), 
                pixel_data, texture_data.data_size);
    
    return message;
}

// Message parsing helpers
bool parseDrawTextMessage(const std::vector<uint8_t>& buffer, MessageHeader& header,
                         DrawTextData& text_data, std::string& text) {
    if (buffer.size() < sizeof(MessageHeader) + sizeof(DrawTextData)) {
        return false;
    }
    
    // Extract header
    std::memcpy(&header, buffer.data(), sizeof(MessageHeader));
    ProtocolHelper::networkToHost(header);
    
    if (!ProtocolHelper::validateHeader(header) || 
        header.data_size < sizeof(DrawTextData)) {
        return false;
    }
    
    // Extract text data
    std::memcpy(&text_data, buffer.data() + sizeof(MessageHeader), sizeof(DrawTextData));
    
    // Validate text length
    if (text_data.text_length > header.data_size - sizeof(DrawTextData)) {
        return false;
    }
    
    // Extract text string
    const char* text_ptr = reinterpret_cast<const char*>(
        buffer.data() + sizeof(MessageHeader) + sizeof(DrawTextData));
    text.assign(text_ptr, text_data.text_length);
    
    return true;
}

bool parseDrawPolygonMessage(const std::vector<uint8_t>& buffer, MessageHeader& header,
                            DrawPolygonData& polygon_data, std::vector<Point>& points) {
    if (buffer.size() < sizeof(MessageHeader) + sizeof(DrawPolygonData)) {
        return false;
    }
    
    // Extract header
    std::memcpy(&header, buffer.data(), sizeof(MessageHeader));
    ProtocolHelper::networkToHost(header);
    
    if (!ProtocolHelper::validateHeader(header) || 
        header.data_size < sizeof(DrawPolygonData)) {
        return false;
    }
    
    // Extract polygon data
    std::memcpy(&polygon_data, buffer.data() + sizeof(MessageHeader), sizeof(DrawPolygonData));
    
    // Validate point count
    size_t expected_points_size = polygon_data.point_count * sizeof(Point);
    if (expected_points_size > header.data_size - sizeof(DrawPolygonData)) {
        return false;
    }
    
    // Extract points
    points.resize(polygon_data.point_count);
    const Point* points_ptr = reinterpret_cast<const Point*>(
        buffer.data() + sizeof(MessageHeader) + sizeof(DrawPolygonData));
    std::memcpy(points.data(), points_ptr, expected_points_size);
    
    return true;
}

bool parseDrawTexturedQuadsMessage(const std::vector<uint8_t>& buffer, MessageHeader& header,
                                  DrawTexturedQuadsData& quad_data, std::vector<TexturedVertex>& vertices) {
    if (buffer.size() < sizeof(MessageHeader) + sizeof(DrawTexturedQuadsData)) {
        return false;
    }
    
    // Extract header
    std::memcpy(&header, buffer.data(), sizeof(MessageHeader));
    ProtocolHelper::networkToHost(header);
    
    if (!ProtocolHelper::validateHeader(header) || 
        header.data_size < sizeof(DrawTexturedQuadsData)) {
        return false;
    }
    
    // Extract quad data
    std::memcpy(&quad_data, buffer.data() + sizeof(MessageHeader), sizeof(DrawTexturedQuadsData));
    
    // Validate vertex count (4 vertices per quad)
    size_t expected_vertex_count = quad_data.quad_count * 4;
    size_t expected_vertices_size = expected_vertex_count * sizeof(TexturedVertex);
    if (expected_vertices_size > header.data_size - sizeof(DrawTexturedQuadsData)) {
        return false;
    }
    
    // Extract vertices
    vertices.resize(expected_vertex_count);
    const TexturedVertex* vertices_ptr = reinterpret_cast<const TexturedVertex*>(
        buffer.data() + sizeof(MessageHeader) + sizeof(DrawTexturedQuadsData));
    std::memcpy(vertices.data(), vertices_ptr, expected_vertices_size);
    
    return true;
}

// Error message helpers
std::string errorCodeToString(ErrorCode code) {
    switch (code) {
        case ErrorCode::SUCCESS: return "Success";
        case ErrorCode::UNKNOWN_COMMAND: return "Unknown command";
        case ErrorCode::INVALID_GC: return "Invalid graphics context";
        case ErrorCode::INVALID_FONT: return "Invalid font";
        case ErrorCode::INVALID_TEXTURE: return "Invalid texture";
        case ErrorCode::INVALID_LAYER: return "Invalid layer";
        case ErrorCode::OUT_OF_MEMORY: return "Out of memory";
        case ErrorCode::PROTOCOL_ERROR: return "Protocol error";
        case ErrorCode::CLIENT_LIMIT_EXCEEDED: return "Client limit exceeded";
        case ErrorCode::PERMISSION_DENIED: return "Permission denied";
        default: return "Unknown error";
    }
}

std::string messageTypeToString(MessageType type) {
    switch (type) {
        case MessageType::CLIENT_HELLO: return "CLIENT_HELLO";
        case MessageType::SERVER_HELLO: return "SERVER_HELLO";
        case MessageType::DRAW_POINT: return "DRAW_POINT";
        case MessageType::DRAW_LINE: return "DRAW_LINE";
        case MessageType::DRAW_RECTANGLE: return "DRAW_RECTANGLE";
        case MessageType::FILL_RECTANGLE: return "FILL_RECTANGLE";
        case MessageType::DRAW_ARC: return "DRAW_ARC";
        case MessageType::FILL_ARC: return "FILL_ARC";
        case MessageType::DRAW_POLYGON: return "DRAW_POLYGON";
        case MessageType::FILL_POLYGON: return "FILL_POLYGON";
        case MessageType::DRAW_TEXT: return "DRAW_TEXT";
        case MessageType::DRAW_IMAGE_STRING: return "DRAW_IMAGE_STRING";
        case MessageType::DRAW_TEXTURED_QUADS: return "DRAW_TEXTURED_QUADS";
        case MessageType::CREATE_GC: return "CREATE_GC";
        case MessageType::FREE_GC: return "FREE_GC";
        case MessageType::SET_FOREGROUND: return "SET_FOREGROUND";
        case MessageType::SET_BACKGROUND: return "SET_BACKGROUND";
        case MessageType::SET_LINE_ATTRIBUTES: return "SET_LINE_ATTRIBUTES";
        case MessageType::SET_FILL_STYLE: return "SET_FILL_STYLE";
        case MessageType::SET_FONT_SIZE: return "SET_FONT_SIZE";
        case MessageType::SET_FUNCTION: return "SET_FUNCTION";
        case MessageType::UPLOAD_FONT_TEXTURE: return "UPLOAD_FONT_TEXTURE";
        case MessageType::CREATE_PIXMAP: return "CREATE_PIXMAP";
        case MessageType::FREE_PIXMAP: return "FREE_PIXMAP";
        case MessageType::CLEAR_LAYER: return "CLEAR_LAYER";
        case MessageType::CLEAR_ALL_LAYERS: return "CLEAR_ALL_LAYERS";
        case MessageType::SET_LAYER_VISIBILITY: return "SET_LAYER_VISIBILITY";
        case MessageType::BATCH_BEGIN: return "BATCH_BEGIN";
        case MessageType::BATCH_END: return "BATCH_END";
        case MessageType::INPUT_EVENT: return "INPUT_EVENT";
        case MessageType::FRAME_CALLBACK: return "FRAME_CALLBACK";
        case MessageType::PING: return "PING";
        case MessageType::PONG: return "PONG";
        case MessageType::ERROR_RESPONSE: return "ERROR_RESPONSE";
        case MessageType::DISCONNECT: return "DISCONNECT";
        default: return "UNKNOWN";
    }
}

} // namespace Kairos