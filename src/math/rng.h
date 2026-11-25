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

    // Fixed point 16.16 range [min_v, max_v]
    int64_t next_fixed_range(int64_t min_v, int64_t max_v) {
        if (min_v >= max_v) return min_v;
        uint64_t r = next() & 0x7FFFFFFFULL;
        return min_v + (int64_t)((r * (uint64_t)(max_v - min_v)) / 0x7FFFFFFFULL);
    }
};

// Deterministic stateless hash PRNG (SplitMix64-inspired, matches GDScript det_rand)
inline float det_rand_float(int64_t seed_val, int64_t s1, int64_t s2 = 0, int64_t s3 = 0) {
    uint32_t h = (uint32_t)seed_val ^ ((uint32_t)s1 * 374761393u) ^ ((uint32_t)s2 * 668265263u) ^ ((uint32_t)s3 * 1013904223u);
    h = (h ^ (h >> 13)) * 1274126177u;
    h = h ^ (h >> 16);
    uint32_t u = h & 0x7FFFFFFFu;
    return (float)u / 2147483647.0f;
}

inline int64_t det_rand_fixed(int64_t seed_val, int64_t s1, int64_t s2 = 0, int64_t s3 = 0) {
    uint32_t h = (uint32_t)seed_val ^ ((uint32_t)s1 * 374761393u) ^ ((uint32_t)s2 * 668265263u) ^ ((uint32_t)s3 * 1013904223u);
    h = (h ^ (h >> 13)) * 1274126177u;
    h = h ^ (h >> 16);
    uint32_t u = h & 0x7FFFFFFFu;
    return (int64_t)(((uint64_t)u * 65536ULL) / 2147483647ULL);
}
