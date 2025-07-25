// shared/include/Constants.hpp
#pragma once

#include <cstdint>

namespace Kairos {

// Protocol version for compatibility checking
constexpr uint32_t PROTOCOL_VERSION = 1;
constexpr uint16_t DEFAULT_SERVER_PORT = 8080;
constexpr const char* DEFAULT_UNIX_SOCKET = "/tmp/kairos_server.sock";

// Constants for graphics operations
namespace Constants {
    // Line styles
    constexpr uint8_t LINE_SOLID = 0;
    constexpr uint8_t LINE_DASHED = 1;
    constexpr uint8_t LINE_DOTTED = 2;
    
    // Cap styles
    constexpr uint8_t CAP_BUTT = 0;
    constexpr uint8_t CAP_ROUND = 1;
    constexpr uint8_t CAP_SQUARE = 2;
    
    // Join styles
    constexpr uint8_t JOIN_MITER = 0;
    constexpr uint8_t JOIN_ROUND = 1;
    constexpr uint8_t JOIN_BEVEL = 2;
    
    // Fill styles
    constexpr uint8_t FILL_SOLID = 0;
    constexpr uint8_t FILL_TILED = 1;
    constexpr uint8_t FILL_STIPPLED = 2;
    
    // Polygon shapes
    constexpr uint8_t COMPLEX = 0;
    constexpr uint8_t NONCONVEX = 1;
    constexpr uint8_t CONVEX = 2;
    
    // Coordinate modes
    constexpr uint8_t COORD_MODE_ORIGIN = 0;
    constexpr uint8_t COORD_MODE_PREVIOUS = 1;
    
    // Graphics functions
    constexpr uint8_t GX_CLEAR = 0;
    constexpr uint8_t GX_AND = 1;
    constexpr uint8_t GX_AND_REVERSE = 2;
    constexpr uint8_t GX_COPY = 3;
    constexpr uint8_t GX_AND_INVERTED = 4;
    constexpr uint8_t GX_NOOP = 5;
    constexpr uint8_t GX_XOR = 6;
    constexpr uint8_t GX_OR = 7;
    constexpr uint8_t GX_NOR = 8;
    constexpr uint8_t GX_EQUIV = 9;
    constexpr uint8_t GX_INVERT = 10;
    constexpr uint8_t GX_OR_REVERSE = 11;
    constexpr uint8_t GX_COPY_INVERTED = 12;
    constexpr uint8_t GX_OR_INVERTED = 13;
    constexpr uint8_t GX_NAND = 14;
    constexpr uint8_t GX_SET = 15;
    
    // Pixel formats
    constexpr uint32_t PIXEL_FORMAT_RGBA8 = 0;
    constexpr uint32_t PIXEL_FORMAT_RGB8 = 1;
    constexpr uint32_t PIXEL_FORMAT_ALPHA8 = 2;
    constexpr uint32_t PIXEL_FORMAT_LUMINANCE8 = 3;
}

// Capability flags
namespace Capabilities {
    constexpr uint32_t BASIC_RENDERING = 0x00000001;
    constexpr uint32_t TEXT_RENDERING = 0x00000002;
    constexpr uint32_t TEXTURED_RENDERING = 0x00000004;
    constexpr uint32_t LAYER_SUPPORT = 0x00000008;
    constexpr uint32_t INPUT_EVENTS = 0x00000010;
    constexpr uint32_t FRAME_CALLBACKS = 0x00000020;
    constexpr uint32_t UNIX_SOCKETS = 0x00000040;
    constexpr uint32_t HIGH_DPI = 0x00000080;
    constexpr uint32_t MULTI_TOUCH = 0x00000100;
}

// System limits
namespace Limits {
    constexpr uint32_t MAX_CLIENTS = 1000;
    constexpr uint32_t MAX_LAYERS = 255;
    constexpr uint32_t MAX_TEXTURES = 10000;
    constexpr uint32_t MAX_FONTS = 1000;
    constexpr uint32_t MAX_MESSAGE_SIZE = 10 * 1024 * 1024; // 10MB
    constexpr uint32_t MAX_BATCH_SIZE = 10000;
    constexpr uint32_t MAX_COMMAND_QUEUE_SIZE = 100000;
    
    // Performance limits
    constexpr uint32_t MAX_FPS = 300;
    constexpr uint32_t MIN_FPS = 10;
    constexpr uint32_t DEFAULT_FPS = 60;
    
    // Memory limits
    constexpr uint32_t DEFAULT_MEMORY_LIMIT_MB = 512;
    constexpr uint32_t MAX_MEMORY_LIMIT_MB = 4096;
    
    // Network limits
    constexpr uint32_t MAX_CONNECTIONS_PER_IP = 10;
    constexpr uint32_t MAX_COMMANDS_PER_SECOND = 10000;
    constexpr uint32_t DEFAULT_RECEIVE_BUFFER_SIZE = 64 * 1024;
    constexpr uint32_t DEFAULT_SEND_BUFFER_SIZE = 64 * 1024;
}

// Default values
namespace Defaults {
    constexpr uint32_t WINDOW_WIDTH = 1920;
    constexpr uint32_t WINDOW_HEIGHT = 1080;
    constexpr uint32_t TARGET_FPS = 60;
    constexpr uint32_t BATCH_SIZE = 1000;
    constexpr uint32_t LAYER_COUNT = 8;
    constexpr float FONT_SIZE = 16.0f;
    constexpr const char* WINDOW_TITLE = "Kairos Graphics Server";
    constexpr const char* LOG_FILE = "kairos_server.log";
}

} // namespace Kairos