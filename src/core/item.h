#pragma once
#include <cstdint>

#ifndef SIM_HEADLESS
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/string.hpp>
#endif

struct ItemAffix {
    int32_t type;
    int32_t value;
};

struct ItemData {
    uint64_t id;
    
    ItemAffix prefixes[3];
    ItemAffix suffixes[3];
    
    int32_t prefix_count;
    int32_t suffix_count;

    static ItemData generate(uint64_t id);
#ifndef SIM_HEADLESS
    godot::Dictionary to_dict() const;
#endif
};
