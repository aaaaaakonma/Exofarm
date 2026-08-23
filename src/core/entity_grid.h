#pragma once
#include <vector>
#include <cstdint>
#include <algorithm>
#include "shapes.h"
#include "entity.h"

class EntityGrid {
public:
    static constexpr int64_t CELL_SHIFT = 25; // 33,554,432 fixed units (512.0)

    EntityGrid() {}

    void build(const std::vector<SimAABB>& walls) {
        _nodes.clear();
        _free_nodes.clear();
        
        int64_t min_x = 0x7FFFFFFFFFFFFFFF;
        int64_t min_y = 0x7FFFFFFFFFFFFFFF;
        int64_t max_x = -0x7FFFFFFFFFFFFFFF;
        int64_t max_y = -0x7FFFFFFFFFFFFFFF;
        
        for (const auto& w : walls) {
            if (w.x < min_x) min_x = w.x;
            if (w.y < min_y) min_y = w.y;
            if (w.x + w.width > max_x) max_x = w.x + w.width;
            if (w.y + w.height > max_y) max_y = w.y + w.height;
        }
        
        if (min_x > max_x) {
            min_x = 0; min_y = 0; max_x = 0; max_y = 0;
        }
        
        _min_cx = (int32_t)(min_x >> CELL_SHIFT);
        _min_cy = (int32_t)(min_y >> CELL_SHIFT);
        int32_t max_cx = (int32_t)(max_x >> CELL_SHIFT);
        int32_t max_cy = (int32_t)(max_y >> CELL_SHIFT);

        _width = std::max(1, max_cx - _min_cx + 1);
        _height = std::max(1, max_cy - _min_cy + 1);

        _cell_heads.assign((size_t)_width * (size_t)_height, 0xFFFFFFFF);
        _cell_states.assign((size_t)_width * (size_t)_height, 0); // 0=INACTIVE, 1=HIBERNATE, 2=ACTIVE
        
        // Pre-allocate nodes roughly based on expected entities
        _nodes.reserve(1024);
        _entity_to_node.clear();
    }

    void clear_entities() {
        _nodes.clear();
        _free_nodes.clear();
        _entity_to_node.clear();
        std::fill(_cell_heads.begin(), _cell_heads.end(), 0xFFFFFFFF);
        std::fill(_cell_states.begin(), _cell_states.end(), 0);
    }

    // Insert entity into the grid based on x, y
    void insert(EntityID entity_id, int64_t x, int64_t y) {
        if (entity_id == 862) { puts("  -> insert: start"); fflush(stdout); }
        int32_t cx = (int32_t)(x >> CELL_SHIFT) - _min_cx;
        int32_t cy = (int32_t)(y >> CELL_SHIFT) - _min_cy;
        if (cx < 0) cx = 0; else if (cx >= _width) cx = _width - 1;
        if (cy < 0) cy = 0; else if (cy >= _height) cy = _height - 1;

        uint32_t grid_idx = (uint32_t)(cy * _width + cx);
        if (entity_id == 862) { printf("  -> insert: grid_idx = %u\n", grid_idx); fflush(stdout); }

        uint32_t node_idx;
        if (!_free_nodes.empty()) {
            node_idx = _free_nodes.back();
            _free_nodes.pop_back();
        } else {
            node_idx = (uint32_t)_nodes.size();
            _nodes.push_back({});
        }

        _nodes[node_idx].entity_id = entity_id;
        _nodes[node_idx].grid_idx = grid_idx;
        _nodes[node_idx].prev = 0xFFFFFFFF;
        _nodes[node_idx].next = _cell_heads[grid_idx];
        
        if (_cell_heads[grid_idx] != 0xFFFFFFFF) {
            _nodes[_cell_heads[grid_idx]].prev = node_idx;
        }
        _cell_heads[grid_idx] = node_idx;
        
        if (entity_id >= _entity_to_node.size()) {
            _entity_to_node.resize(entity_id + 128, 0xFFFFFFFF);
        }
        _entity_to_node[entity_id] = node_idx;
        if (entity_id == 862) { puts("  -> insert: done"); fflush(stdout); }
    }

    // Move an entity if its cell changed
    void update(EntityID entity_id, int64_t old_x, int64_t old_y, int64_t new_x, int64_t new_y) {
        int32_t new_cx = (int32_t)(new_x >> CELL_SHIFT) - _min_cx;
        int32_t new_cy = (int32_t)(new_y >> CELL_SHIFT) - _min_cy;
        if (new_cx < 0) new_cx = 0; else if (new_cx >= _width) new_cx = _width - 1;
        if (new_cy < 0) new_cy = 0; else if (new_cy >= _height) new_cy = _height - 1;
        uint32_t new_grid_idx = (uint32_t)(new_cy * _width + new_cx);

        if (entity_id < _entity_to_node.size()) {
            uint32_t node_idx = _entity_to_node[entity_id];
            if (node_idx != 0xFFFFFFFF && _nodes[node_idx].grid_idx == new_grid_idx) {
                return; // Cell didn't change
            }
        }
        
        if (entity_id == 862) { puts("  -> calling remove"); fflush(stdout); }
        remove(entity_id);
        if (entity_id == 862) { puts("  -> calling insert"); fflush(stdout); }
        insert(entity_id, new_x, new_y);
        if (entity_id == 862) { puts("  -> update finished"); fflush(stdout); }
    }

    // Remove entity from grid
    void remove(EntityID entity_id) {
        if (entity_id >= _entity_to_node.size()) return;
        uint32_t node_idx = _entity_to_node[entity_id];
        if (node_idx == 0xFFFFFFFF) return;
        if (entity_id == 862) { puts("  -> remove: start"); fflush(stdout); }

        uint32_t grid_idx = _nodes[node_idx].grid_idx;
        if (entity_id == 862) { printf("  -> remove: grid_idx = %u\n", grid_idx); fflush(stdout); }

        uint32_t prev = _nodes[node_idx].prev;
        uint32_t next = _nodes[node_idx].next;

        if (prev != 0xFFFFFFFF) {
            _nodes[prev].next = next;
        } else {
            _cell_heads[grid_idx] = next;
        }

        if (next != 0xFFFFFFFF) {
            _nodes[next].prev = prev;
        }

        _nodes[node_idx].entity_id = INVALID_ENTITY;
        _free_nodes.push_back(node_idx);
        _entity_to_node[entity_id] = 0xFFFFFFFF;
        if (entity_id == 862) { puts("  -> remove: done"); fflush(stdout); }
    }

    template<typename Callback>
    void query_cell(int32_t cx, int32_t cy, Callback&& cb) const {
        if (cx < 0 || cx >= _width || cy < 0 || cy >= _height) return;
        uint32_t grid_idx = (uint32_t)(cy * _width + cx);
        uint32_t node_idx = _cell_heads[grid_idx];
        while (node_idx != 0xFFFFFFFF) {
            cb(_nodes[node_idx].entity_id);
            node_idx = _nodes[node_idx].next;
        }
    }

    template<typename Callback>
    void query(const Experimental::AABB& aabb, Callback&& cb) const {
        int32_t min_cx = (int32_t)(aabb.min_x >> CELL_SHIFT) - _min_cx;
        int32_t min_cy = (int32_t)(aabb.min_y >> CELL_SHIFT) - _min_cy;
        int32_t max_cx = (int32_t)(aabb.max_x >> CELL_SHIFT) - _min_cx;
        int32_t max_cy = (int32_t)(aabb.max_y >> CELL_SHIFT) - _min_cy;

        if (min_cx < 0) min_cx = 0;
        if (min_cy < 0) min_cy = 0;
        if (max_cx >= _width) max_cx = _width - 1;
        if (max_cy >= _height) max_cy = _height - 1;

        for (int32_t cy = min_cy; cy <= max_cy; ++cy) {
            for (int32_t cx = min_cx; cx <= max_cx; ++cx) {
                uint32_t grid_idx = (uint32_t)(cy * _width + cx);
                uint32_t node_idx = _cell_heads[grid_idx];
                while (node_idx != 0xFFFFFFFF) {
                    cb(_nodes[node_idx].entity_id);
                    node_idx = _nodes[node_idx].next;
                }
            }
        }
    }

    int64_t get_cell_occupancy(int64_t x, int64_t y) const {
        int32_t cx = (int32_t)(x >> CELL_SHIFT) - _min_cx;
        int32_t cy = (int32_t)(y >> CELL_SHIFT) - _min_cy;
        if (cx < 0 || cx >= _width || cy < 0 || cy >= _height) return 0;
        uint32_t grid_idx = (uint32_t)(cy * _width + cx);
        uint32_t node_idx = _cell_heads[grid_idx];
        int64_t count = 0;
        while (node_idx != 0xFFFFFFFF) {
            count++;
            node_idx = _nodes[node_idx].next;
        }
        return count;
    }

    int32_t get_min_cx() const { return _min_cx; }
    int32_t get_min_cy() const { return _min_cy; }
    int32_t get_width() const { return _width; }
    int32_t get_height() const { return _height; }
    
    uint8_t get_cell_state(int32_t cx, int32_t cy) const {
        if (cx < 0 || cx >= _width || cy < 0 || cy >= _height) return 0;
        return _cell_states[cy * _width + cx];
    }
    
    void set_cell_state(int32_t cx, int32_t cy, uint8_t state) {
        if (cx < 0 || cx >= _width || cy < 0 || cy >= _height) return;
        _cell_states[cy * _width + cx] = state;
    }

    void resize_entity_map(size_t size) {
        _entity_to_node.resize(size, 0xFFFFFFFF);
    }

private:
    struct EntityNode {
        EntityID entity_id;
        uint32_t grid_idx;
        uint32_t prev;
        uint32_t next;
    };

    std::vector<uint32_t> _cell_heads;
    std::vector<uint8_t> _cell_states; // Grid cell states
    std::vector<EntityNode> _nodes;
    std::vector<uint32_t> _free_nodes;
    std::vector<uint32_t> _entity_to_node; // Map entity ID to its node in the linked list
    
    int32_t _min_cx = 0;
    int32_t _min_cy = 0;
    int32_t _width = 0;
    int32_t _height = 0;
};
