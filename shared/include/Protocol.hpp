// shared/include/Protocol.hpp
#pragma once

#include <Types.hpp>
#include <Constants.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace Kairos {

// Message types
enum class MessageType : uint8_t {
    // Handshake
    CLIENT_HELLO = 0x01,
    SERVER_HELLO = 0x02,
    
    // Drawing commands
    DRAW_POINT = 0x10,
    DRAW_LINE = 0x11,
    DRAW_RECTANGLE = 0x12,
    FILL_RECTANGLE = 0x13,
    DRAW_ARC = 0x14,
    FILL_ARC = 0x15,
    DRAW_POLYGON = 0x16,
    FILL_POLYGON = 0x17,
    DRAW_TEXT = 0x18,
    DRAW_IMAGE_STRING = 0x19,
    DRAW_TEXTURED_QUADS = 0x1A,
    
    // Graphics context
    CREATE_GC = 0x20,
    FREE_GC = 0x21,
    SET_FOREGROUND = 0x22,
    SET_BACKGROUND = 0x23,
    SET_LINE_ATTRIBUTES = 0x24,
    SET_FILL_STYLE = 0x25,
    SET_FONT_SIZE = 0x26,
    SET_FUNCTION = 0x27,
    
    // Resource management
    UPLOAD_FONT_TEXTURE = 0x30,
    CREATE_PIXMAP = 0x31,
    FREE_PIXMAP = 0x32,
    
    // Layer management
    CLEAR_LAYER = 0x40,
    CLEAR_ALL_LAYERS = 0x41,
    SET_LAYER_VISIBILITY = 0x42,
    BATCH_BEGIN = 0x43,
    BATCH_END = 0x44,
    
    // Events (server to client)
    INPUT_EVENT = 0x50,
    FRAME_CALLBACK = 0x51,
    
    // System
    PING = 0xF0,
    PONG = 0xF1,
    ERROR_RESPONSE = 0xFE,
    DISCONNECT = 0xFF
};

// Message header (fixed size)
struct MessageHeader {
    uint32_t magic;          // 0x4B41524F ("KARO")
    uint32_t protocol_version;
    MessageType type;
    uint8_t layer_id;
    uint16_t reserved;
    uint32_t client_id;
    uint32_t sequence;
    uint32_t data_size;      // Size of data following header
    uint64_t timestamp;      // Microseconds since epoch
    
    MessageHeader() 
        : magic(0x4B41524F), protocol_version(PROTOCOL_VERSION), 
          type(MessageType::PING), layer_id(0), reserved(0),
          client_id(0), sequence(0), data_size(0), timestamp(0) {}
} __attribute__((packed));

// Handshake messages
struct ClientHello {
    char client_name[64];    // Application name
    uint32_t client_version;
    uint32_t requested_layers;
    uint32_t capabilities;   // Feature flags
} __attribute__((packed));

struct ServerHello {
    uint32_t server_version;
    uint32_t max_clients;
    uint32_t assigned_client_id;
    uint32_t server_capabilities;
    uint32_t max_layers;
} __attribute__((packed));

// Drawing command data structures
struct DrawPointData {
    uint32_t gc_id;
    Point position;
} __attribute__((packed));

struct DrawLineData {
    uint32_t gc_id;
    Point start;
    Point end;
} __attribute__((packed));

struct DrawRectangleData {
    uint32_t gc_id;
    Point position;
    float width;
    float height;
} __attribute__((packed));

struct DrawArcData {
    uint32_t gc_id;
    Point center;
    float width;
    float height;
    int16_t angle1;
    int16_t angle2;
} __attribute__((packed));

struct DrawPolygonData {
    uint32_t gc_id;
    uint8_t shape;           // Complex, Convex, etc.
    uint8_t coord_mode;      // Origin or Previous
    uint16_t point_count;
    // Followed by Point[point_count]
} __attribute__((packed));

struct DrawTextData {
    uint32_t gc_id;
    uint32_t font_id;
    Point position;
    float font_size;
    uint16_t text_length;
    uint16_t reserved;
    // Followed by UTF-8 text[text_length]
} __attribute__((packed));

// Textured rendering (for font atlases)
struct DrawTexturedQuadsData {
    uint32_t gc_id;
    uint32_t texture_id;
    uint32_t quad_count;
    uint32_t reserved;
    // Followed by TexturedVertex[quad_count * 4]
} __attribute__((packed));

// Graphics context management
struct CreateGCData {
    uint32_t drawable_id;
    // Initial GC values can be added here
} __attribute__((packed));

struct SetColorData {
    uint32_t gc_id;
    Color color;
} __attribute__((packed));

struct SetLineAttributesData {
    uint32_t gc_id;
    uint8_t line_width;
    uint8_t line_style;      // Solid, Dashed, etc.
    uint8_t cap_style;       // Butt, Round, Square
    uint8_t join_style;      // Miter, Round, Bevel
} __attribute__((packed));

struct SetFontSizeData {
    uint32_t gc_id;
    uint32_t font_id;
    float font_size;
} __attribute__((packed));

// Resource management
struct FontTextureData {
    uint32_t texture_id;
    uint32_t width;
    uint32_t height;
    uint32_t format;         // Pixel format (RGBA8, etc.)
    uint32_t data_size;
    // Followed by pixel data[data_size]
} __attribute__((packed));

struct CreatePixmapData {
    uint32_t pixmap_id;
    uint32_t width;
    uint32_t height;
    uint8_t depth;
    uint8_t reserved[3];
} __attribute__((packed));

// Layer management
struct LayerVisibilityData {
    uint8_t layer_id;
    uint8_t visible;
    uint16_t reserved;
} __attribute__((packed));

// Error handling
struct ErrorResponse {
    ErrorCode error_code;
    uint32_t original_sequence; // Sequence of failed command
    char error_message[128];    // Human readable error
} __attribute__((packed));

// Network utilities
struct PingData {
    uint64_t client_timestamp;
} __attribute__((packed));

struct PongData {
    uint64_t client_timestamp;  // Echo back
    uint64_t server_timestamp;  // Server time
    uint32_t server_load;       // CPU usage %
    uint32_t queue_depth;       // Commands queued
} __attribute__((packed));

// Helper functions for protocol handling
class ProtocolHelper {
public:
    // Validate message header
    static bool validateHeader(const MessageHeader& header);
    
    // Calculate message size including header
    static size_t getMessageSize(const MessageHeader& header);
    
    // Get current timestamp in microseconds
    static uint64_t getCurrentTimestamp();
    
    // Template logging with formatting
    template<typename... Args>
    static std::vector<uint8_t> serialize(const MessageHeader& header, const Args&... data);
    
    template<typename T>
    static bool deserialize(const std::vector<uint8_t>& buffer, MessageHeader& header, T& data);
    
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
    
    // Network byte order conversion
    static void hostToNetwork(MessageHeader& header);
    static void networkToHost(MessageHeader& header);
};

} // namespace Kairos