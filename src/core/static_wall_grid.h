#pragma once
#include <vector>
#include <cstdint>
#include <algorithm>
#include "shapes.h"

class StaticWallGrid {
public:
    static constexpr int64_t CELL_SHIFT = 22; // 4,194,304 fixed units (64.0)
    static constexpr uint32_t NUM_BUCKETS = 262139; // Prime number near 262144

    StaticWallGrid() {}

    void build(const std::vector<SimAABB>& walls) {
        _nodes.clear();
        _last_query_id.assign(walls.size(), 0);
        _current_query_id = 0;
        
        _cell_heads.assign(NUM_BUCKETS, 0xFFFFFFFF);

        if (walls.empty()) {
            return;
        }

        // 2. Populate
        for (uint32_t i = 0; i < (uint32_t)walls.size(); ++i) {
            const auto& w = walls[i];
            int32_t cx1 = (int32_t)(w.x >> CELL_SHIFT);
            int32_t cx2 = (int32_t)((w.x + w.width - 1) >> CELL_SHIFT);
            int32_t cy1 = (int32_t)(w.y >> CELL_SHIFT);
            int32_t cy2 = (int32_t)((w.y + w.height - 1) >> CELL_SHIFT);

            for (int32_t cy = cy1; cy <= cy2; ++cy) {
                for (int32_t cx = cx1; cx <= cx2; ++cx) {
                    uint32_t grid_idx = ((uint32_t)cx * 73856093 ^ (uint32_t)cy * 19349663) % NUM_BUCKETS;
                    uint32_t node_idx = (uint32_t)_nodes.size();
                    _nodes.push_back({i, _cell_heads[grid_idx]});
                    _cell_heads[grid_idx] = node_idx;
                }
            }
        }
    }

    void build_polygons(const std::vector<SimPolygon>& polygons) {
        _nodes.clear();
        _last_query_id.assign(polygons.size(), 0);
        _current_query_id = 0;
        
        _cell_heads.assign(NUM_BUCKETS, 0xFFFFFFFF);

        if (polygons.empty()) {
            return;
        }

        // 2. Populate
        for (uint32_t i = 0; i < (uint32_t)polygons.size(); ++i) {
            const auto& w = polygons[i].bounds;
            int32_t cx1 = (int32_t)(w.x >> CELL_SHIFT);
            int32_t cx2 = (int32_t)((w.x + w.width - 1) >> CELL_SHIFT);
            int32_t cy1 = (int32_t)(w.y >> CELL_SHIFT);
            int32_t cy2 = (int32_t)((w.y + w.height - 1) >> CELL_SHIFT);

            for (int32_t cy = cy1; cy <= cy2; ++cy) {
                for (int32_t cx = cx1; cx <= cx2; ++cx) {
                    uint32_t grid_idx = ((uint32_t)cx * 73856093 ^ (uint32_t)cy * 19349663) % NUM_BUCKETS;
                    uint32_t node_idx = (uint32_t)_nodes.size();
                    _nodes.push_back({i, _cell_heads[grid_idx]});
                    _cell_heads[grid_idx] = node_idx;
                }
            }
        }
    }

    template<typename Callback>
    void query(int64_t x, int64_t y, int64_t radius, Callback&& cb) const {
        if (_cell_heads.empty()) return;

        int32_t q_min_cx = (int32_t)((x - radius) >> CELL_SHIFT);
        int32_t q_max_cx = (int32_t)((x + radius) >> CELL_SHIFT);
        int32_t q_min_cy = (int32_t)((y - radius) >> CELL_SHIFT);
        int32_t q_max_cy = (int32_t)((y + radius) >> CELL_SHIFT);

        _current_query_id++;
        if (_current_query_id == 0) {
            std::fill(_last_query_id.begin(), _last_query_id.end(), 0);
            _current_query_id = 1;
        }

        for (int32_t cy = q_min_cy; cy <= q_max_cy; ++cy) {
            for (int32_t cx = q_min_cx; cx <= q_max_cx; ++cx) {
                uint32_t grid_idx = ((uint32_t)cx * 73856093 ^ (uint32_t)cy * 19349663) % NUM_BUCKETS;
                uint32_t node_idx = _cell_heads[grid_idx];
                while (node_idx != 0xFFFFFFFF) {
                    uint32_t wall_idx = _nodes[node_idx].wall_idx;
                    if (_last_query_id[wall_idx] != _current_query_id) {
                        _last_query_id[wall_idx] = _current_query_id;
                        cb(wall_idx);
                    }
                    node_idx = _nodes[node_idx].next;
                }
            }
        }
    }

private:
    struct WallNode {
        uint32_t wall_idx;
        uint32_t next;
    };

    std::vector<uint32_t> _cell_heads;
    std::vector<WallNode> _nodes;

    mutable std::vector<uint32_t> _last_query_id;
    mutable uint32_t _current_query_id = 0;
};
