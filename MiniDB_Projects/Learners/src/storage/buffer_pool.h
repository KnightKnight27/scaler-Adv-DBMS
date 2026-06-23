#ifndef BUFFER_POOL_H
#define BUFFER_POOL_H

#include "page_manager.h"
#include "page.h"
#include <vector>
#include <string>
#include <unordered_map>
#include "../compat.h"

struct BufferFrame {
    std::string table_name;
    int page_id{-1};
    std::vector<uint8_t> data;
    int pin_count{0};
    bool is_dirty{false};
    int ref_bit{0};
    bool occupied{false};

    BufferFrame() : data(PAGE_SIZE, 0) {}
};

class BufferPool {
private:
    int capacity;
    std::vector<BufferFrame> frames;
    std::unordered_map<std::string, int> page_map;
    int clock_hand{0};
    Mutex mu;

    int find_victim();
    std::string make_key(const std::string& table_name, int page_id) const;

public:
    PageManager& page_manager;
    int hit_count{0};
    int miss_count{0};

    BufferPool(PageManager& pm, int capacity = 10);
    ~BufferPool() = default;

    std::pair<int, uint8_t*> fetch_page(const std::string& table_name, int page_id);
    void unpin_page(const std::string& table_name, int page_id, bool is_dirty);
    void flush_page(const std::string& table_name, int page_id);
    void flush_all();
};

#endif
