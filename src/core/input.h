#pragma once
#include <cstdint>
#include <cstring>

// Tool selection enum for player farming / woodcutting interactions
enum class PlayerTool : uint8_t {
    NONE         = 0,
    AXE          = 1,
    HOE          = 2,
    WATERING_CAN = 3,
    SEED_BAG     = 4,
    SICKLE       = 5
};

// One frame of player input for the simulation
#pragma pack(push, 1)
struct InputFrame {
    enum class Kind : uint8_t { 
        PlayerInput, 
        EquipItem, 
        UnequipItem, 
        PlaceStructure, 
        RemoveStructure,
        SpawnBot,
        SetBotJob,
        UploadBotProgram,
        WireBotPorts,
        ModifyTerrain
    } kind = Kind::PlayerInput;
    uint32_t tick = 0;          // absolute tick counter, monotonic

    union Payload {
        struct PlayerData {
            int32_t  move_x;        // fixed point direction vector
            int32_t  move_y;
            int64_t  aim_x;         // fixed point mouse X (world-space, needs int64)
            int64_t  aim_y;         // fixed point mouse Y (world-space, needs int64)
            uint32_t actions;       // bitmask — attack, dodge, interact etc
            uint8_t  selected_tool; // PlayerTool enum
            uint8_t  crop_type;     // crop type (default 1 if 0)
            uint8_t  crop_prefix;
            uint8_t  crop_suffix;
            uint8_t  crop_implicit;
            uint8_t  _pad[3];
        } player;

        struct EventData {
            uint32_t event_entity;
            int32_t  event_slot;
            uint64_t event_item_id;
        } event;

        struct StructureData {
            int32_t  x;
            int32_t  y;
            uint8_t  structure_type;
        } structure;

        struct TerrainData {
            int32_t  x;
            int32_t  y;
            int32_t  delta_elev;
        } terrain;

        struct BotData {
            uint32_t bot_id;
            uint8_t  bot_type;
            uint8_t  job;
            int32_t  x;
            int32_t  y;
            uint8_t  seed_type;
            int32_t  sensor_x;
            int32_t  sensor_y;
            int32_t  actuator_x;
            int32_t  actuator_y;
        } bot;

        struct BotProgramData {
            uint32_t bot_id;
            uint16_t opcodes[32];
            uint16_t program_size;
        } bot_program;
    } payload;

    InputFrame() {
        std::memset(&payload, 0, sizeof(payload));
    }
};
#pragma pack(pop)

// Action bitmask flags
enum InputAction : uint32_t {
    ACTION_NONE            = 0,
    ACTION_ATTACK          = 1 << 0,
    ACTION_DODGE           = 1 << 1,
    ACTION_INTERACT        = 1 << 2,
    ACTION_ABILITY_1       = 1 << 3,
    ACTION_ABILITY_2       = 1 << 4,
    ACTION_ABILITY_3       = 1 << 5,
    ACTION_ABILITY_4       = 1 << 6,
    ACTION_TERRAFORM_RAISE = 1 << 7,
    ACTION_TERRAFORM_LOWER = 1 << 8,
    ACTION_CHOP            = 1 << 9,
    ACTION_TILL            = 1 << 10,
    ACTION_WATER           = 1 << 11,
    ACTION_PLANT           = 1 << 12,
    ACTION_HARVEST         = 1 << 13,
};

inline uint32_t make_player_action(uint32_t base_action, PlayerTool tool = PlayerTool::NONE, uint8_t crop_type = 0) {
    return base_action | (static_cast<uint32_t>(tool) << 16) | (static_cast<uint32_t>(crop_type) << 20);
}
