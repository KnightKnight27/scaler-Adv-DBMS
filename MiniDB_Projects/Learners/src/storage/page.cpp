#include "page.h"

Page::Page(uint32_t page_id, uint8_t* data_ptr) : page_id(page_id), data(data_ptr) {
    if (data != nullptr) {
        unpack_header();
        if (free_space_ptr == 0) {
            free_space_ptr = PAGE_SIZE;
            num_slots = 0;
            pack_header();
        }
    } else {
        num_slots = 0;
        free_space_ptr = PAGE_SIZE;
    }
}

void Page::unpack_header() {
    std::memcpy(&page_id, data, 4);
    std::memcpy(&num_slots, data + 4, 2);
    std::memcpy(&free_space_ptr, data + 6, 2);
}

void Page::pack_header() {
    std::memcpy(data, &page_id, 4);
    std::memcpy(data + 4, &num_slots, 2);
    std::memcpy(data + 6, &free_space_ptr, 2);
}

std::pair<uint16_t, uint16_t> Page::get_slot(int slot_id) const {
    if (slot_id < 0 || slot_id >= num_slots) {
        return {0, 0};
    }
    uint16_t offset, length;
    std::memcpy(&offset, data + HEADER_SIZE + slot_id * SLOT_SIZE, 2);
    std::memcpy(&length, data + HEADER_SIZE + slot_id * SLOT_SIZE + 2, 2);
    return {offset, length};
}

void Page::set_slot(int slot_id, uint16_t offset, uint16_t length) {
    std::memcpy(data + HEADER_SIZE + slot_id * SLOT_SIZE, &offset, 2);
    std::memcpy(data + HEADER_SIZE + slot_id * SLOT_SIZE + 2, &length, 2);
}

bool Page::has_room_for(int length) const {
    int needed_slot_dir_end = HEADER_SIZE + (num_slots + 1) * SLOT_SIZE;
    return free_space_ptr - needed_slot_dir_end >= length;
}

int Page::insert_record(const std::string& record) {
    int length = record.length();
    if (!has_room_for(length)) {
        return -1;
    }
    free_space_ptr -= length;
    std::memcpy(data + free_space_ptr, record.data(), length);

    int slot_id = num_slots;
    set_slot(slot_id, free_space_ptr, length);
    num_slots++;
    pack_header();
    return slot_id;
}

bool Page::insert_record_at(int slot_id, const std::string& record) {
    int length = record.length();
    int needed_slots = (slot_id >= num_slots) ? (slot_id + 1 - num_slots) : 0;
    int needed_bytes = length + needed_slots * SLOT_SIZE;
    if (free_space_ptr - (HEADER_SIZE + num_slots * SLOT_SIZE) < needed_bytes) {
        return false;
    }
    free_space_ptr -= length;
    std::memcpy(data + free_space_ptr, record.data(), length);
    set_slot(slot_id, free_space_ptr, length);
    if (slot_id >= num_slots) {
        num_slots = slot_id + 1;
    }
    pack_header();
    return true;
}

std::string Page::get_record(int slot_id) const {
    auto slot = get_slot(slot_id);
    if (slot.first == 0 && slot.second == 0) {
        return "";
    }
    return std::string((char*)(data + slot.first), slot.second);
}

bool Page::delete_record(int slot_id) {
    auto slot = get_slot(slot_id);
    if (slot.first == 0 && slot.second == 0) {
        return false;
    }
    set_slot(slot_id, 0, 0);
    compact();
    return true;
}

void Page::compact() {
    std::vector<std::pair<int, std::pair<uint16_t, uint16_t>>> active_slots;
    for (int i = 0; i < num_slots; ++i) {
        auto slot = get_slot(i);
        if (slot.first != 0 && slot.second != 0) {
            active_slots.push_back({i, slot});
        }
    }
    
    // Sort by offset descending (records closest to page end first)
    std::sort(active_slots.begin(), active_slots.end(), [](const std::pair<int, std::pair<uint16_t, uint16_t>>& a, const std::pair<int, std::pair<uint16_t, uint16_t>>& b) {
        return a.second.first > b.second.first;
    });

    uint16_t new_free_space_ptr = PAGE_SIZE;
    std::vector<uint8_t> temp_buffer(PAGE_SIZE);

    for (const auto& entry : active_slots) {
        int idx = entry.first;
        uint16_t offset = entry.second.first;
        uint16_t length = entry.second.second;

        new_free_space_ptr -= length;
        std::memcpy(temp_buffer.data() + new_free_space_ptr, data + offset, length);
        set_slot(idx, new_free_space_ptr, length);
    }

    // Copy records back
    std::memcpy(data + new_free_space_ptr, temp_buffer.data() + new_free_space_ptr, PAGE_SIZE - new_free_space_ptr);
    
    // Zero out old slots that were deleted
    std::unordered_set<int> active_indices;
    for (const auto& entry : active_slots) {
        active_indices.insert(entry.first);
    }
    for (int i = 0; i < num_slots; ++i) {
        if (active_indices.find(i) == active_indices.end()) {
            set_slot(i, 0, 0);
        }
    }

    free_space_ptr = new_free_space_ptr;
    pack_header();
}
