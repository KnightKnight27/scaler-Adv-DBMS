#include <iostream>
#include <vector>
#include <unordered_map>
#include <thread>

template <typename T>
struct Frame {
    T    key{};
    bool ref      = false;
    bool occupied = false;
};

template <typename T>
class ClockSweep {
public:
    ClockSweep(int maxNumber = 4): maxCacheSize(maxNumber), frames(maxNumber) {};

    T getKey(T key) {
        auto it = page_to_index.find(key);
        if (it != page_to_index.end()) {
            frames[it->second].ref = true;   // second chance
            std::cout << "Hit:   key=" << key << " frame=" << it->second << "\n";
            print_state();
            return key;
        }
        std::cout << "Miss:  key=" << key << "\n";
        return T{};
    }

    void putKey(T key) {
        // Already in cache — refresh ref bit
        if (page_to_index.count(key)) {
            frames[page_to_index[key]].ref = true;
            std::cout << "Already cached: key=" << key << "\n";
            return;
        }

        std::cout << "Fault: key=" << key << "\n";

        // Find an empty frame first
        for (uint i = 0; i < maxCacheSize; ++i) {
            if (!frames[i].occupied) {
                load_into_frame(i, key);
                print_state();
                return;
            }
        }

        // Cache full: clock sweep to find victim
        while (true) {
            Frame<T>& curr = frames[hand];
            if (!curr.ref) {
                std::cout << "  Evicting key=" << curr.key << " from frame=" << hand << "\n";
                page_to_index.erase(curr.key);
                load_into_frame(hand, key);
                hand = (hand + 1) % maxCacheSize;
                break;
            } else {
                curr.ref = false;
                hand = (hand + 1) % maxCacheSize;
            }
        }
        print_state();
    }

private:
    uint maxCacheSize{0u};
    std::vector<Frame<T>> frames;
    std::unordered_map<T, uint> page_to_index;
    uint hand{0};
    std::thread bgClockThread;

    void load_into_frame(uint idx, T key) {
        frames[idx].key      = key;
        frames[idx].ref      = true;
        frames[idx].occupied = true;
        page_to_index[key]   = idx;
    }

    void print_state() const {
        std::cout << "Frames: ";
        for (const auto& f : frames) {
            if (!f.occupied) std::cout << "[ _,0] ";
            else             std::cout << "[" << f.key << "," << f.ref << "] ";
        }
        std::cout << " Hand=" << hand << "\n\n";
    }
};

int main(){
    ClockSweep<int> clockSweep(4);

    std::vector<int> refs = {7, 0, 1, 2, 0, 3, 0, 4, 2, 3, 0, 3, 2};
    for (int page : refs) {
        clockSweep.putKey(page);
    }
    
    return 0;
}
