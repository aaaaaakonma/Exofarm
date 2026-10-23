#pragma once


#ifndef SIM_HEADLESS
#include <godot_cpp/variant/utility_functions.hpp>
using namespace godot;
#else
#include <iostream>
struct UtilityFunctions {
    template<typename... Args>
    static void print(Args&&... args) {
        (std::cout << ... << args) << "\n";
    }
};
#endif

#include <vector>
#include <cstdint>
#include <string>
#include <fstream>
#include <map>

enum class TileAnchorType : uint32_t {
    EAST = 0,
    SOUTH = 1,
    WEST = 2,
    NORTH = 3,
    SPECIAL = 4
};

enum class RoomCategory : uint8_t {
    NONE = 0,
    HALLWAY = 1,
    JUNCTION = 2,
    SMALL = 3,
    MAJOR = 4,
    BOSS = 5,
    SPAWN = 6
};

struct TileAnchor {
    int64_t x, y;
    TileAnchorType type;
    uint64_t name_hash = 0;
    
};

struct TileBlueprint {
    uint64_t id;
    uint64_t type_hash = 0;
    std::string type_name;
    RoomCategory category = RoomCategory::NONE;
    int64_t width;
    int64_t height;
    int64_t bound_x = 0;
    int64_t bound_y = 0;
    
    std::vector<TileAnchor> anchors;
    
};

class TileRegistry {
public:
    bool load_from_file(const std::string& path) {
        UtilityFunctions::print("TileRegistry: Opening file: ", path.c_str());
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            UtilityFunctions::print("TileRegistry: Failed to open file!");
            return false;
        }

        char magic[4];
        file.read(magic, 4);
        if (magic[0] != 'T' || magic[1] != 'I' || magic[2] != 'L' || magic[3] != 'E') {
            UtilityFunctions::print("TileRegistry: Invalid Magic Number!");
            return false;
        }

        uint32_t version;
        file.read(reinterpret_cast<char*>(&version), 4);
        if (version < 1 || version > 8) {
            UtilityFunctions::print("TileRegistry: Unsupported Version: ", (int)version);
            return false;
        }

        uint32_t room_count;
        file.read(reinterpret_cast<char*>(&room_count), 4);
        UtilityFunctions::print("TileRegistry: Loading ", (int)room_count, " rooms...");

        _blueprints.clear();
        for (uint32_t i = 0; i < room_count; ++i) {
            TileBlueprint bp;
            file.read(reinterpret_cast<char*>(&bp.id), 8);
            
            if (version >= 7) {
                uint32_t name_len;
                file.read(reinterpret_cast<char*>(&name_len), 4);
                if (name_len > 0 && name_len < 1024) {
                    std::vector<char> name_buf(name_len);
                    file.read(name_buf.data(), name_len);
                    bp.type_name = std::string(name_buf.data(), name_len);
                }
                file.read(reinterpret_cast<char*>(&bp.type_hash), 8);
            } else {
                bp.type_hash = 0;
                bp.type_name = "Unknown";
            }

            if (version >= 8) {
                file.read(reinterpret_cast<char*>(&bp.category), 1);
            } else {
                bp.category = RoomCategory::NONE;
            }

            file.read(reinterpret_cast<char*>(&bp.width), 8);
            file.read(reinterpret_cast<char*>(&bp.height), 8);

            if (version >= 4) {
                file.read(reinterpret_cast<char*>(&bp.bound_x), 8);
                file.read(reinterpret_cast<char*>(&bp.bound_y), 8);
            }

            uint32_t wall_count;
            file.read(reinterpret_cast<char*>(&wall_count), 4);
                        for (uint32_t w = 0; w < wall_count; ++w) {
                int64_t dummy[4];
                file.read(reinterpret_cast<char*>(dummy), 32);
            }

            uint32_t anchor_count;
            file.read(reinterpret_cast<char*>(&anchor_count), 4);
            bp.anchors.resize(anchor_count);
            for (uint32_t a = 0; a < anchor_count; ++a) {
                file.read(reinterpret_cast<char*>(&bp.anchors[a].x), 8);
                file.read(reinterpret_cast<char*>(&bp.anchors[a].y), 8);
                file.read(reinterpret_cast<char*>(&bp.anchors[a].type), 4);
                
                if (version >= 5) {
                    file.read(reinterpret_cast<char*>(&bp.anchors[a].name_hash), 8);
                } else {
                    bp.anchors[a].name_hash = 0;
                }
                
                if (version >= 3) {
                    int64_t dummy_auto_wall[4];
                    file.read(reinterpret_cast<char*>(dummy_auto_wall), 32);
                }
            }

            if (version >= 2) {
                uint32_t poly_count;
                file.read(reinterpret_cast<char*>(&poly_count), 4);
                for (uint32_t p = 0; p < poly_count; ++p) {
                    uint32_t v_count;
                    file.read(reinterpret_cast<char*>(&v_count), 4);
                    
                    int64_t xy[2];
                    file.read(reinterpret_cast<char*>(xy), 16);
                    
                    for (uint32_t v = 0; v < v_count; ++v) {
                        int64_t vxy[2];
                        file.read(reinterpret_cast<char*>(vxy), 16);
                    }
                }
            }

            _blueprints[bp.id] = std::move(bp);
        }

        return true;
    }

    const std::map<uint64_t, TileBlueprint>& get_blueprints() const { return _blueprints; }
    
    const TileBlueprint* get_blueprint(uint64_t id) const {
        auto it = _blueprints.find(id);
        if (it != _blueprints.end()) return &it->second;
        return nullptr;
    }

    void clear() { _blueprints.clear(); }

private:
    std::map<uint64_t, TileBlueprint> _blueprints;
};
