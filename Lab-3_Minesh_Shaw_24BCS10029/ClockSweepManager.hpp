#pragma once

#include <vector>
#include <unordered_map>
#include <iostream>
#include <stdexcept>
#include <iomanip>

struct BufferFrame {
    int page_id = -1;
    int usage_count = 0;
    int pin_count = 0;
    bool is_valid = false;
};

class ClockSweepManager {
private:
    size_t pool_size;
    std::vector<BufferFrame> frames;
    
    std::unordered_map<int, size_t> page_table;
    
    size_t clock_hand;

    size_t FindVictim();

public:
    explicit ClockSweepManager(size_t size);

    void AccessPage(int page_id);
    void PinPage(int page_id);
    void UnpinPage(int page_id);
    
    void PrintState() const;
};