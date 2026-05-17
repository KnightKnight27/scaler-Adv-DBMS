#include <iostream>
#include <vector>
#include <unordered_map>

using namespace std;

class ClockSweepCache {
private:
    struct PageFrame {
        int page_id;
        bool reference_bit;
        
        PageFrame(int id) : page_id(id), reference_bit(true) {}
    };

    int capacity;
    int clock_hand;
    vector<PageFrame> frames;
    unordered_map<int, int> page_map;

public:
    ClockSweepCache(int cap) : capacity(cap), clock_hand(0) {}

    void accessPage(int page_id) {
        cout << "Accessing page " << page_id << "...\n";
        
        if (page_map.find(page_id) != page_map.end()) {
            cout << "  -> Page Hit! Setting reference bit to 1.\n";
            int frame_index = page_map[page_id];
            frames[frame_index].reference_bit = true;
            return;
        }

        cout << "  -> Page Miss!\n";
        if (frames.size() < capacity) {
            frames.emplace_back(page_id);
            page_map[page_id] = frames.size() - 1;
            cout << "  -> Placed page " << page_id << " in an empty frame.\n";
            return;
        }

        while (true) {
            if (frames[clock_hand].reference_bit == true) {
                frames[clock_hand].reference_bit = false;
                clock_hand = (clock_hand + 1) % capacity;
            } else {
                int victim_page = frames[clock_hand].page_id;
                cout << "  -> Evicting victim page " << victim_page << "\n";
                
                page_map.erase(victim_page);
                
                frames[clock_hand].page_id = page_id;
                frames[clock_hand].reference_bit = true;
                page_map[page_id] = clock_hand;
                cout << "  -> Loaded page " << page_id << " into frame " << clock_hand << ".\n";

                clock_hand = (clock_hand + 1) % capacity;
                break;
            }
        }
    }

    void display() {
        cout << "Current Cache State (Capacity: " << capacity << "):\n";
        if (frames.empty()) {
            cout << "  [Empty]\n";
            return;
        }
        for (int i = 0; i < frames.size(); ++i) {
            if (i == clock_hand) cout << "-> ";
            else cout << "   ";
            cout << "Frame " << i << ": Page " << frames[i].page_id 
                 << " [RefBit: " << frames[i].reference_bit << "]\n";
        }
    }
};

int main() {
    ClockSweepCache cache(3);

    int pages[] = {1, 2, 3, 2, 4, 1, 5, 2};
    int n = sizeof(pages) / sizeof(pages[0]);

    for (int i = 0; i < n; ++i) {
        cache.accessPage(pages[i]);
        cache.display();
    }

    return 0;
}
