#pragma once
#include <cstdint>

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
        WireBotPorts
    } kind = Kind::PlayerInput;
    uint32_t tick;          // absolute tick counter, monotonic

    union Payload {
        struct PlayerData {
            int32_t  move_x;        // fixed point direction vector
            int32_t  move_y;
            int64_t  aim_x;         // fixed point mouse X (world-space, needs int64)
            int64_t  aim_y;         // fixed point mouse Y (world-space, needs int64)
            uint32_t actions;       // bitmask — attack, dodge, interact etc
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
};
#pragma pack(pop)

// Action bitmask flags
enum InputAction : uint32_t {
    ACTION_NONE      = 0,
    ACTION_ATTACK    = 1 << 0,
    ACTION_DODGE     = 1 << 1,
    ACTION_INTERACT  = 1 << 2,
    ACTION_ABILITY_1 = 1 << 3,
    ACTION_ABILITY_2 = 1 << 4,
    ACTION_ABILITY_3 = 1 << 5,
    ACTION_ABILITY_4 = 1 << 6,
};
