#pragma once
#include <cstdint>

// SplitMix64 PRNG - perfect for fast, deterministic, stateless generation
class SplitMix64 {
public:
    uint64_t state;

    SplitMix64(uint64_t seed) : state(seed) {}

    uint64_t next() {
        uint64_t z = (state += 0x9e3779b97f4a7c15ULL);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    }
    
    // Helper for ranges [min, max] inclusive
    int32_t next_range(int32_t min, int32_t max) {
        if (min >= max) return min;
        uint64_t r = next();
        return min + (r % (max - min + 1));
    }
};
