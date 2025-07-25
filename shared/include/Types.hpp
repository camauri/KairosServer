// shared/include/Types.hpp
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <chrono>

namespace Kairos {

// Forward declarations
class Server;
class RaylibRenderer;
class NetworkManager;
class CommandProcessor;
class LayerManager;
class FontManager;

// Basic geometric types
struct Point {
    float x, y;
    
    Point() : x(0), y(0) {}
    Point(float x_, float y_) : x(x_), y(y_) {}
    
    Point operator+(const Point& other) const { return {x + other.x, y + other.y}; }
    Point operator-(const Point& other) const { return {x - other.x, y - other.y}; }
    Point operator*(float scale) const { return {x * scale, y * scale}; }
};

struct Rectangle {
    float x, y, width, height;
    
    Rectangle() : x(0), y(0), width(0), height(0) {}
    Rectangle(float x_, float y_, float w_, float h_) : x(x_), y(y_), width(w_), height(h_) {}
    
    bool contains(const Point& point) const {
        return point.x >= x && point.x <= x + width && 
               point.y >= y && point.y <= y + height;
    }
};

struct Color {
    uint8_t r, g, b, a;
    uint32_t rgba;
    
    Color() : r(255), g(255), b(255), a(255) { 
        rgba = (r << 24) | (g << 16) | (b << 8) | a; 
    }
    
    Color(uint8_t r_, uint8_t g_, uint8_t b_, uint8_t a_ = 255) 
        : r(r_), g(g_), b(b_), a(a_) {
        rgba = (r << 24) | (g << 16) | (b << 8) | a;
    }
    
    Color(uint32_t rgba_) : rgba(rgba_) {
        r = (rgba >> 24) & 0xFF;
        g = (rgba >> 16) & 0xFF;
        b = (rgba >> 8) & 0xFF;
        a = rgba & 0xFF;
    }
    
    // Common colors
    static const Color WHITE;
    static const Color BLACK;
    static const Color RED;
    static const Color GREEN;
    static const Color BLUE;
    static const Color TRANSPARENT;
};

// Texture vertex for batched rendering
struct TexturedVertex {
    float x, y;              // Position
    float u, v;              // Texture coordinates
    uint32_t color;          // RGBA color
    
    TexturedVertex() : x(0), y(0), u(0), v(0), color(0xFFFFFFFF) {}
    TexturedVertex(float x_, float y_, float u_, float v_, uint32_t color_ = 0xFFFFFFFF)
        : x(x_), y(y_), u(u_), v(v_), color(color_) {}
};

// Input event types
enum class InputEventType : uint8_t {
    KEY_PRESS = 0x01,
    KEY_RELEASE = 0x02,
    MOUSE_MOVE = 0x03,
    MOUSE_PRESS = 0x04,
    MOUSE_RELEASE = 0x05,
    MOUSE_WHEEL = 0x06,
    TOUCH_BEGIN = 0x07,
    TOUCH_MOVE = 0x08,
    TOUCH_END = 0x09
};

// Input event structure
struct InputEvent {
    InputEventType type;
    uint8_t button;          // Mouse button or key code
    uint16_t modifiers;      // Ctrl, Shift, Alt flags
    Point position;          // Mouse/touch position
    float wheel_delta;       // Mouse wheel delta
    uint64_t timestamp;      // Event timestamp
} __attribute__((packed));

// Frame callback structure
struct FrameCallback {
    uint32_t frame_number;
    uint64_t frame_time;     // VSync timestamp
    float frame_rate;        // Current FPS
    uint32_t dropped_frames; // Performance metric
} __attribute__((packed));

// Error codes
enum class ErrorCode : uint32_t {
    SUCCESS = 0,
    UNKNOWN_COMMAND = 1,
    INVALID_GC = 2,
    INVALID_FONT = 3,
    INVALID_TEXTURE = 4,
    INVALID_LAYER = 5,
    OUT_OF_MEMORY = 6,
    PROTOCOL_ERROR = 7,
    CLIENT_LIMIT_EXCEEDED = 8,
    PERMISSION_DENIED = 9
};

} // namespace Kairos