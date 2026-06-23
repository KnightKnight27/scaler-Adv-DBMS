#include "buffer_pool.h"
#include <stdexcept>
#include <iostream>

BufferPool::BufferPool(PageManager& pm, int capacity) : page_manager(pm), capacity(capacity), frames(capacity) {}

std::string BufferPool::make_key(const std::string& table_name, int page_id) const {
    return table_name + "|" + std::to_string(page_id);
}

std::pair<int, uint8_t*> BufferPool::fetch_page(const std::string& table_name, int page_id) {
    LockGuard lck(mu);
    std::string key = make_key(table_name, page_id);
    
    // 1. Check if page is already in buffer pool
    auto it = page_map.find(key);
    if (it != page_map.end()) {
        int idx = it->second;
        frames[idx].pin_count++;
        frames[idx].ref_bit = 1;
        hit_count++;
        return {idx, frames[idx].data.data()};
    }

    miss_count++;

    // 2. Find a victim frame to evict
    int victim_idx = find_victim();
    if (victim_idx == -1) {
        throw std::runtime_error("Buffer pool is full! All pages are pinned.");
    }

    BufferFrame& victim = frames[victim_idx];

    // 3. Evict old page if the frame was occupied
    if (victim.occupied) {
        std::string old_key = make_key(victim.table_name, victim.page_id);
        if (victim.is_dirty) {
            page_manager.write_page(victim.table_name, victim.page_id, victim.data.data());
        }
        page_map.erase(old_key);
    }

    // 4. Load new page into the victim frame
    page_manager.read_page(table_name, page_id, victim.data.data());
    victim.table_name = table_name;
    victim.page_id = page_id;
    victim.pin_count = 1;
    victim.is_dirty = false;
    victim.ref_bit = 1;
    victim.occupied = true;

    page_map[key] = victim_idx;
    return {victim_idx, victim.data.data()};
}

void BufferPool::unpin_page(const std::string& table_name, int page_id, bool is_dirty) {
    LockGuard lck(mu);
    std::string key = make_key(table_name, page_id);
    auto it = page_map.find(key);
    if (it == page_map.end()) {
        return;
    }
    int idx = it->second;
    if (frames[idx].pin_count > 0) {
        frames[idx].pin_count--;
    }
    if (is_dirty) {
        frames[idx].is_dirty = true;
    }
}

void BufferPool::flush_page(const std::string& table_name, int page_id) {
    LockGuard lck(mu);
    std::string key = make_key(table_name, page_id);
    auto it = page_map.find(key);
    if (it != page_map.end()) {
        int idx = it->second;
        if (frames[idx].is_dirty) {
            page_manager.write_page(table_name, page_id, frames[idx].data.data());
            frames[idx].is_dirty = false;
        }
    }
}

void BufferPool::flush_all() {
    LockGuard lck(mu);
    for (auto& frame : frames) {
        if (frame.occupied && frame.is_dirty) {
            page_manager.write_page(frame.table_name, frame.page_id, frame.data.data());
            frame.is_dirty = false;
        }
    }
}

int BufferPool::find_victim() {
    // Clock sweep algorithm: scan up to 2 full iterations
    for (int iter = 0; iter < 2 * capacity; ++iter) {
        int idx = clock_hand;
        clock_hand = (clock_hand + 1) % capacity;

        if (!frames[idx].occupied) {
            return idx; // Found empty slot immediately
        }

        if (frames[idx].pin_count == 0) {
            if (frames[idx].ref_bit == 1) {
                frames[idx].ref_bit = 0; // Give second chance
            } else {
                return idx; // Found victim
            }
        }
    }
    return -1;
}
