#pragma once
#include <cstdint>

static constexpr uint32_t PAGE_SIZE = 4096;

struct Page {
    uint32_t id    = 0;
    bool     dirty = false;
    uint8_t  data[PAGE_SIZE]{};
};
