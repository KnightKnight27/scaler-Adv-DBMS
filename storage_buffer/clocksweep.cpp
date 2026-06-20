#include <algorithm>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

struct Slot {
    int page = -1;
    int usage = 0;
    bool pinned = false;
    bool dirty = false;
};

class ClockBuffer {
public:
    explicit ClockBuffer(std::size_t frame_count) : slots_(frame_count) {
        if (frame_count == 0) {
            throw std::invalid_argument("ClockBuffer needs at least one frame");
        }
    }

    int access(int page_id, bool pin_page = false) {
        auto hit = lookup(page_id);
        if (hit) {
            Slot& slot = slots_[*hit];
            slot.usage = std::min(slot.usage + 1, kUsageCap);
            slot.pinned = slot.pinned || pin_page;
            std::cout << "hit   page=" << page_id << " frame=" << *hit
                      << " usage=" << slot.usage << '\n';
            return static_cast<int>(*hit);
        }

        auto target = pick_frame();
        if (!target) {
            std::cout << "miss  page=" << page_id << " no victim, every frame is pinned\n";
            return -1;
        }

        Slot& slot = slots_[*target];
        if (slot.page != -1) {
            std::cout << "evict page=" << slot.page << " frame=" << *target;
            if (slot.dirty) {
                std::cout << " writeback";
            }
            std::cout << '\n';
            page_table_.erase(slot.page);
        }

        slot = Slot{page_id, 1, pin_page, false};
        page_table_[page_id] = *target;
        std::cout << "miss  page=" << page_id << " frame=" << *target << '\n';
        return static_cast<int>(*target);
    }

    void pin(int page_id) {
        if (auto frame = lookup(page_id)) {
            slots_[*frame].pinned = true;
        }
    }

    void release(int page_id, bool mark_dirty = false) {
        if (auto frame = lookup(page_id)) {
            slots_[*frame].pinned = false;
            slots_[*frame].dirty = slots_[*frame].dirty || mark_dirty;
        }
    }

    void dump() const {
        std::cout << "\nclock hand=" << hand_ << '\n';
        for (std::size_t i = 0; i < slots_.size(); ++i) {
            const Slot& slot = slots_[i];
            std::cout << "  frame " << i
                      << " page=" << (slot.page < 0 ? std::string("--") : std::to_string(slot.page))
                      << " usage=" << slot.usage
                      << (slot.pinned ? " pinned" : "")
                      << (slot.dirty ? " dirty" : "")
                      << (i == hand_ ? " <- next" : "") << '\n';
        }
        std::cout << '\n';
    }

private:
    static constexpr int kUsageCap = 5;

    std::vector<Slot> slots_;
    std::unordered_map<int, std::size_t> page_table_;
    std::size_t hand_ = 0;

    std::optional<std::size_t> lookup(int page_id) const {
        auto it = page_table_.find(page_id);
        if (it == page_table_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    std::optional<std::size_t> pick_frame() {
        const std::size_t budget = slots_.size() * (kUsageCap + 1);
        for (std::size_t scanned = 0; scanned < budget; ++scanned) {
            const std::size_t current = hand_;
            Slot& slot = slots_[current];
            hand_ = (hand_ + 1) % slots_.size();

            if (slot.pinned) {
                continue;
            }
            if (slot.page == -1 || slot.usage == 0) {
                return current;
            }
            --slot.usage;
        }
        return std::nullopt;
    }
};

int main() {
    ClockBuffer buffer(4);

    for (int page : {10, 20, 30, 40, 10, 20}) {
        buffer.access(page);
    }
    buffer.pin(30);
    buffer.access(50);
    buffer.dump();

    buffer.release(30, true);
    for (int page : {60, 70, 10, 80}) {
        buffer.access(page);
    }
    buffer.dump();

    return 0;
}
