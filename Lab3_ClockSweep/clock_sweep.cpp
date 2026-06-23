#include <iostream>
#include <vector>
#include <unordered_map>
#include <memory>
#include <shared_mutex>
#include <mutex>
#include <cassert>

using page_id_t = int;
using frame_id_t = int;

constexpr size_t PAGE_SIZE = 4096;

struct Page {
    page_id_t page_id = -1;
    char data[PAGE_SIZE] = {0};
};

class DiskManager {
private:
    std::unordered_map<page_id_t, std::vector<char>> disk_storage;
public:
    DiskManager() = default;

    void ReadPage(page_id_t page_id, char* page_data) {
        auto it = disk_storage.find(page_id);
        if (it == disk_storage.end()) {
            std::vector<char> dummy(PAGE_SIZE, 0);
            std::string msg = "PageData_" + std::to_string(page_id);
            std::copy(msg.begin(), msg.end(), dummy.begin());
            disk_storage[page_id] = dummy;
            std::copy(dummy.begin(), dummy.end(), page_data);
        } else {
            std::copy(it->second.begin(), it->second.end(), page_data);
        }
        std::cout << "[DiskManager] Read page " << page_id << " from disk." << std::endl;
    }

    void WritePage(page_id_t page_id, const char* page_data) {
        std::vector<char> data(PAGE_SIZE);
        std::copy(page_data, page_data + PAGE_SIZE, data.begin());
        disk_storage[page_id] = data;
        std::cout << "[DiskManager] Written page " << page_id << " to disk." << std::endl;
    }
};

class BufferPoolManager {
private:
    size_t pool_size;
    DiskManager disk_manager;
    std::vector<Page> pages;
    std::vector<page_id_t> page_ids;
    std::vector<bool> reference_bits;
    std::vector<int> pin_counts;
    std::vector<bool> dirty_bits;
    std::unordered_map<page_id_t, frame_id_t> page_table;
    frame_id_t clock_hand;
    std::shared_mutex latch;

    bool Victim(frame_id_t* frame_id) {
        size_t scans = 0;
        while (scans < 2 * pool_size) {
            if (pin_counts[clock_hand] == 0) {
                if (reference_bits[clock_hand]) {
                    reference_bits[clock_hand] = false;
                } else {
                    *frame_id = clock_hand;
                    clock_hand = (clock_hand + 1) % pool_size;
                    return true;
                }
            }
            clock_hand = (clock_hand + 1) % pool_size;
            scans++;
        }
        return false;
    }

public:
    BufferPoolManager(size_t pool_size)
        : pool_size(pool_size),
          pages(pool_size),
          page_ids(pool_size, -1),
          reference_bits(pool_size, false),
          pin_counts(pool_size, 0),
          dirty_bits(pool_size, false),
          clock_hand(0) {}

    Page* FetchPage(page_id_t page_id) {
        std::unique_lock<std::shared_mutex> lock(latch);

        auto it = page_table.find(page_id);
        if (it != page_table.end()) {
            frame_id_t frame_id = it->second;
            pin_counts[frame_id]++;
            reference_bits[frame_id] = true;
            std::cout << "[BufferPoolManager] Page " << page_id << " found in Frame " << frame_id 
                      << " (new pin_count: " << pin_counts[frame_id] << ")" << std::endl;
            return &pages[frame_id];
        }

        frame_id_t frame_id = -1;
        for (size_t i = 0; i < pool_size; ++i) {
            if (page_ids[i] == -1) {
                frame_id = static_cast<frame_id_t>(i);
                break;
            }
        }

        if (frame_id == -1) {
            if (!Victim(&frame_id)) {
                std::cerr << "[BufferPoolManager] ERROR: Buffer pool is full and all pages are pinned!" << std::endl;
                return nullptr;
            }
            page_id_t victim_page_id = page_ids[frame_id];
            std::cout << "[BufferPoolManager] Evicting Page " << victim_page_id << " from Frame " << frame_id << std::endl;
            if (dirty_bits[frame_id]) {
                disk_manager.WritePage(victim_page_id, pages[frame_id].data);
            }
            page_table.erase(victim_page_id);
        }

        Page& page = pages[frame_id];
        page.page_id = page_id;
        disk_manager.ReadPage(page_id, page.data);

        page_ids[frame_id] = page_id;
        pin_counts[frame_id] = 1;
        reference_bits[frame_id] = true;
        dirty_bits[frame_id] = false;
        page_table[page_id] = frame_id;

        std::cout << "[BufferPoolManager] Loaded Page " << page_id << " into Frame " << frame_id << std::endl;
        return &page;
    }

    bool UnpinPage(page_id_t page_id, bool is_dirty) {
        std::unique_lock<std::shared_mutex> lock(latch);
        auto it = page_table.find(page_id);
        if (it == page_table.end()) {
            return false;
        }
        frame_id_t frame_id = it->second;
        if (pin_counts[frame_id] <= 0) {
            return false;
        }
        pin_counts[frame_id]--;
        if (is_dirty) {
            dirty_bits[frame_id] = true;
        }
        std::cout << "[BufferPoolManager] Unpinned Page " << page_id << " in Frame " << frame_id 
                  << " (is_dirty: " << (is_dirty ? "true" : "false") 
                  << ", remaining pin_count: " << pin_counts[frame_id] << ")" << std::endl;
        return true;
    }

    bool FlushPage(page_id_t page_id) {
        std::unique_lock<std::shared_mutex> lock(latch);
        auto it = page_table.find(page_id);
        if (it == page_table.end()) {
            return false;
        }
        frame_id_t frame_id = it->second;
        if (dirty_bits[frame_id]) {
            disk_manager.WritePage(page_id, pages[frame_id].data);
            dirty_bits[frame_id] = false;
        }
        return true;
    }

    void PrintState() {
        std::shared_lock<std::shared_mutex> lock(latch);
        std::cout << "\n=== Buffer Pool State ===" << std::endl;
        for (size_t i = 0; i < pool_size; ++i) {
            std::cout << "Frame " << i << " -> PageID: " << page_ids[i]
                      << ", RefBit: " << reference_bits[i]
                      << ", PinCount: " << pin_counts[i]
                      << ", DirtyBit: " << dirty_bits[i];
            if (clock_hand == static_cast<frame_id_t>(i)) {
                std::cout << " <--- Clock Hand";
            }
            std::cout << std::endl;
        }
        std::cout << "=========================\n" << std::endl;
    }
};

int main() {
    std::cout << "=== Starting Lab 3: ClockSweep Buffer Pool Manager ===" << std::endl;
    BufferPoolManager bpm(3); 

    auto p1 = bpm.FetchPage(1);
    auto p2 = bpm.FetchPage(2);
    auto p3 = bpm.FetchPage(3);
    bpm.PrintState();

    bpm.UnpinPage(1, false);
    bpm.UnpinPage(2, true);
    bpm.PrintState();

    auto p4 = bpm.FetchPage(4);
    bpm.PrintState();

    bpm.UnpinPage(3, false);
    auto p5 = bpm.FetchPage(5);
    bpm.PrintState();

    bpm.UnpinPage(4, false);
    bpm.UnpinPage(5, false);
    return 0;
}
