#pragma once
#include "fixed.h"
#include <vector>

// ===== SHAPES =====

#pragma pack(push, 1)

struct SimCircle {
    int64_t x, y, radius;
    int64_t user_data;
};

struct SimAABB {
    int64_t x, y, width, height;
    int64_t user_data;

    bool operator==(const SimAABB& o) const {
        return x == o.x && y == o.y && width == o.width && height == o.height && user_data == o.user_data;
    }
};

#pragma pack(pop)

struct SimPolygon {
    int64_t x, y; // Origin for rendering/offsetting
    std::vector<int64_t> vx;
    std::vector<int64_t> vy;
    SimAABB bounds;
    int64_t user_data;
};

// ===== RESULTS =====


struct OverlapResult {
    bool    hit         = false;
    int64_t normal_x    = 0;
    int64_t normal_y    = 0;
    int64_t penetration = 0;
    int64_t user_data   = 0;
};

namespace Experimental {
struct AABB {
    int64_t min_x, min_y;
    int64_t max_x, max_y;

    static AABB from_min_max(int64_t min_x, int64_t min_y, int64_t max_x, int64_t max_y) {
        return {min_x, min_y, max_x, max_y};
    }

    FORCE_INLINE bool overlaps(const AABB& other) const {
        return (max_x >= other.min_x) & (min_x <= other.max_x) &
               (max_y >= other.min_y) & (min_y <= other.max_y);
    }

    FORCE_INLINE bool contains(const AABB& other) const {
        return (min_x <= other.min_x) & (max_x >= other.max_x) &
               (min_y <= other.min_y) & (max_y >= other.max_y);
    }

    FORCE_INLINE void merge(const AABB& other) {
        min_x = min_fixed(min_x, other.min_x);
        min_y = min_fixed(min_y, other.min_y);
        max_x = max_fixed(max_x, other.max_x);
        max_y = max_fixed(max_y, other.max_y);
    }

    static FORCE_INLINE AABB merge(const AABB& a, const AABB& b) {
        return {
            min_fixed(a.min_x, b.min_x),
            min_fixed(a.min_y, b.min_y),
            max_fixed(a.max_x, b.max_x),
            max_fixed(a.max_y, b.max_y)
        };
    }

    FORCE_INLINE int64_t get_perimeter() const {
        return (max_x - min_x + max_y - min_y) << 1;
    }
};
}


