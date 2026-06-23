#pragma once
#include <cstdint>
#include <cstring>

constexpr int PAGE_SIZE = 4096;

class Page {
public:
    int page_id;
    int pin_count;
    bool is_dirty;
    char data[PAGE_SIZE];

    Page() {
        reset();
    }

    void reset() {
        page_id = -1;
        pin_count = 0;
        is_dirty = false;
        std::memset(data, 0, PAGE_SIZE);
    }
};
