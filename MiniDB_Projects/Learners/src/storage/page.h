#ifndef PAGE_H
#define PAGE_H

#include <vector>
#include <string>
#include <cstring>
#include <cstdint>
#include <algorithm>
#include <unordered_set>

constexpr int PAGE_SIZE = 4096;
constexpr int HEADER_SIZE = 8; // page_id (4), num_slots (2), free_space_ptr (2)
constexpr int SLOT_SIZE = 4;   // offset (2), length (2)

class Page {
public:
    uint32_t page_id;
    uint16_t num_slots;
    uint16_t free_space_ptr;
    uint8_t* data;

    Page(uint32_t page_id, uint8_t* data_ptr = nullptr);
    ~Page() = default;

    void unpack_header();
    void pack_header();

    std::pair<uint16_t, uint16_t> get_slot(int slot_id) const;
    void set_slot(int slot_id, uint16_t offset, uint16_t length);

    bool has_room_for(int length) const;
    int insert_record(const std::string& record);
    bool insert_record_at(int slot_id, const std::string& record);
    std::string get_record(int slot_id) const;
    bool delete_record(int slot_id);
    void compact();
};

#endif
