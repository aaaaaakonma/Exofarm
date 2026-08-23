#include "item.h"
#include "rng.h"

ItemData ItemData::generate(uint64_t id) {
    ItemData item;
    item.id = id;
    
    SplitMix64 main_rng(id);
    
    // 1st split: Prefix vs Suffix seeds
    uint64_t prefix_seed = main_rng.next();
    uint64_t suffix_seed = main_rng.next();

    // 2nd split: Prefix Generation
    SplitMix64 prefix_rng(prefix_seed);
    item.prefix_count = prefix_rng.next_range(1, 3); // 1 to 3 prefixes
    
    for (int i = 0; i < item.prefix_count; ++i) {
        uint64_t slot_seed = prefix_rng.next();
        SplitMix64 slot_rng(slot_seed);
        
        // 3rd split: Type and Value seeds
        uint64_t type_seed = slot_rng.next();
        uint64_t value_seed = slot_rng.next();
        
        item.prefixes[i].type = SplitMix64(type_seed).next_range(0, 10);
        item.prefixes[i].value = SplitMix64(value_seed).next_range(1, 100);
    }

    // 2nd split: Suffix Generation
    SplitMix64 suffix_rng(suffix_seed);
    item.suffix_count = suffix_rng.next_range(1, 3); // 1 to 3 suffixes
    
    for (int i = 0; i < item.suffix_count; ++i) {
        uint64_t slot_seed = suffix_rng.next();
        SplitMix64 slot_rng(slot_seed);
        
        // 3rd split: Type and Value seeds
        uint64_t type_seed = slot_rng.next();
        uint64_t value_seed = slot_rng.next();
        
        item.suffixes[i].type = SplitMix64(type_seed).next_range(0, 10);
        item.suffixes[i].value = SplitMix64(value_seed).next_range(1, 100);
    }
    
    return item;
}

#ifndef SIM_HEADLESS
godot::Dictionary ItemData::to_dict() const {
    godot::Dictionary dict;
    dict[godot::String("id")] = (int64_t)id; // Safe for Godot GDScript int
    
    godot::Array pre_arr;
    for (int i = 0; i < prefix_count; ++i) {
        godot::Dictionary a;
        a[godot::String("type")] = prefixes[i].type;
        a[godot::String("value")] = prefixes[i].value;
        pre_arr.push_back(a);
    }
    dict[godot::String("prefixes")] = pre_arr;
    
    godot::Array suf_arr;
    for (int i = 0; i < suffix_count; ++i) {
        godot::Dictionary a;
        a[godot::String("type")] = suffixes[i].type;
        a[godot::String("value")] = suffixes[i].value;
        suf_arr.push_back(a);
    }
    dict[godot::String("suffixes")] = suf_arr;
    
    return dict;
}
#endif
