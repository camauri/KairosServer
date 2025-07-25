// shared/include/Commands.hpp
#pragma once

#include <Types.hpp>
#include <Constants.hpp>
#include <string>
#include <vector>
#include <cstdint>

namespace Kairos {

/**
 * @brief Command validation and utilities
 */
class CommandValidator {
public:
    static bool validateDrawCommand(const Point& position, float width = 0, float height = 0);
    static bool validateTextCommand(const std::string& text, uint32_t font_id);
    static bool validateColorValues(const Color& color);
    static bool validateLayerId(uint8_t layer_id);
    static bool validateClientId(uint32_t client_id);
};

/**
 * @brief Command factory for creating protocol messages
 */
class CommandFactory {
public:
    // Drawing command factories
    static std::vector<uint8_t> createDrawPointCommand(uint32_t client_id, uint8_t layer_id,
                                                       const Point& position, const Color& color);
    
    static std::vector<uint8_t> createDrawLineCommand(uint32_t client_id, uint8_t layer_id,
                                                     const Point& start, const Point& end, 
                                                     const Color& color);
    
    static std::vector<uint8_t> createDrawRectangleCommand(uint32_t client_id, uint8_t layer_id,
                                                          const Point& position, float width, float height,
                                                          const Color& color, bool filled = true);
    
    static std::vector<uint8_t> createDrawTextCommand(uint32_t client_id, uint8_t layer_id,
                                                     const Point& position, const std::string& text,
                                                     uint32_t font_id, float font_size, const Color& color);
    
    static std::vector<uint8_t> createDrawTexturedQuadsCommand(uint32_t client_id, uint8_t layer_id,
                                                              const std::vector<TexturedVertex>& vertices,
                                                              uint32_t texture_id);
    
    // Layer command factories
    static std::vector<uint8_t> createClearLayerCommand(uint32_t client_id, uint8_t layer_id);
    static std::vector<uint8_t> createSetLayerVisibilityCommand(uint32_t client_id, uint8_t layer_id, bool visible);
    
    // System command factories
    static std::vector<uint8_t> createPingCommand(uint32_t client_id);
    static std::vector<uint8_t> createDisconnectCommand(uint32_t client_id);
};

/**
 * @brief Command parser for processing incoming messages
 */
class CommandParser {
public:
    struct ParsedCommand {
        MessageType type;
        uint32_t client_id;
        uint8_t layer_id;
        uint32_t sequence;
        std::vector<uint8_t> data;
        bool valid = false;
    };
    
    static ParsedCommand parseMessage(const std::vector<uint8_t>& message_buffer);
    static bool validateCommandData(MessageType type, const std::vector<uint8_t>& data);
    
    // Specific command parsers
    static bool parseDrawPointCommand(const std::vector<uint8_t>& data, DrawPointData& point_data);
    static bool parseDrawLineCommand(const std::vector<uint8_t>& data, DrawLineData& line_data);
    static bool parseDrawRectangleCommand(const std::vector<uint8_t>& data, DrawRectangleData& rect_data);
    static bool parseDrawTextCommand(const std::vector<uint8_t>& data, DrawTextData& text_data, std::string& text);
    static bool parseDrawTexturedQuadsCommand(const std::vector<uint8_t>& data, DrawTexturedQuadsData& quad_data,
                                             std::vector<TexturedVertex>& vertices);
};

/**
 * @brief Command statistics and monitoring
 */
class CommandStats {
public:
    struct Stats {
        uint64_t total_commands = 0;
        uint64_t draw_commands = 0;
        uint64_t layer_commands = 0;
        uint64_t system_commands = 0;
        uint64_t invalid_commands = 0;
        uint64_t bytes_processed = 0;
        
        // Per-type breakdown
        uint64_t points_drawn = 0;
        uint64_t lines_drawn = 0;
        uint64_t rectangles_drawn = 0;
        uint64_t text_drawn = 0;
        uint64_t textured_quads_drawn = 0;
        
        double avg_command_size = 0.0;
        double commands_per_second = 0.0;
    };
    
    static void recordCommand(MessageType type, size_t size);
    static void recordInvalidCommand();
    static Stats getStats();
    static void resetStats();
    
private:
    static Stats s_stats;
    static std::chrono::steady_clock::time_point s_last_update;
};

/**
 * @brief Command serialization utilities
 */
namespace CommandSerialization {
    // Serialize basic types
    void writeUInt8(std::vector<uint8_t>& buffer, uint8_t value);
    void writeUInt16(std::vector<uint8_t>& buffer, uint16_t value);
    void writeUInt32(std::vector<uint8_t>& buffer, uint32_t value);
    void writeFloat(std::vector<uint8_t>& buffer, float value);
    void writePoint(std::vector<uint8_t>& buffer, const Point& point);
    void writeColor(std::vector<uint8_t>& buffer, const Color& color);
    void writeString(std::vector<uint8_t>& buffer, const std::string& str);
    
    // Deserialize basic types
    bool readUInt8(const std::vector<uint8_t>& buffer, size_t& offset, uint8_t& value);
    bool readUInt16(const std::vector<uint8_t>& buffer, size_t& offset, uint16_t& value);
    bool readUInt32(const std::vector<uint8_t>& buffer, size_t& offset, uint32_t& value);
    bool readFloat(const std::vector<uint8_t>& buffer, size_t& offset, float& value);
    bool readPoint(const std::vector<uint8_t>& buffer, size_t& offset, Point& point);
    bool readColor(const std::vector<uint8_t>& buffer, size_t& offset, Color& color);
    bool readString(const std::vector<uint8_t>& buffer, size_t& offset, std::string& str, size_t max_length);
}

} // namespace Kairos