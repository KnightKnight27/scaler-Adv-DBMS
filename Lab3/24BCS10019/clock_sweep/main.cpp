#include <iostream>
#include <vector>
#include <unordered_map>

using namespace std;

// Structure to represent a frame in the buffer pool
struct Frame {
    int page_id = -1;
    int ref_count = 0;   // Usage/reference counter
    bool in_use = false; // Pinned flag
    bool occupied = false; // Valid flag
};

// Clock Sweep Buffer Manager
class ClockSweepBuffer {
private:
    vector<Frame> pool;
    unordered_map<int, int> page_table; // Maps page_id to frame index
    int capacity;
    int clock_hand;

public:
    // Initialize the buffer pool
    ClockSweepBuffer(int cap) : capacity(cap), clock_hand(0) {
        pool.resize(capacity);
    }

    // Insert a page into the buffer pool
    void put(int page_id) {
        if (page_table.find(page_id) != page_table.end()) {
            // Page is already in the buffer
            return;
        }

        // Find a free or evictable frame (Clock Sweep Logic)
        while (true) {
            Frame& current = pool[clock_hand];
            
            if (!current.occupied) {
                // Free frame found
                current.page_id = page_id;
                current.ref_count = 1;
                current.in_use = false;
                current.occupied = true;
                page_table[page_id] = clock_hand;
                clock_hand = (clock_hand + 1) % capacity;
                return;
            } else if (!current.in_use && current.ref_count == 0) {
                // Evictable frame found (eviction logic)
                page_table.erase(current.page_id);
                current.page_id = page_id;
                current.ref_count = 1;
                current.in_use = false;
                current.occupied = true;
                page_table[page_id] = clock_hand;
                clock_hand = (clock_hand + 1) % capacity;
                return;
            } else if (!current.in_use && current.ref_count > 0) {
                // Page is not pinned but recently used. Give it a second chance.
                current.ref_count--;
            }
            
            // Move clock hand forward
            clock_hand = (clock_hand + 1) % capacity;
        }
    }

    // Access a page (pins the page)
    bool get(int page_id) {
        if (page_table.find(page_id) != page_table.end()) {
            int index = page_table[page_id];
            pool[index].ref_count = 1;
            pool[index].in_use = true;
            return true;
        }
        return false; // Cache miss
    }

    // Release a page (unpins the page)
    void release(int page_id) {
        if (page_table.find(page_id) != page_table.end()) {
            int index = page_table[page_id];
            pool[index].in_use = false;
        }
    }

    // Print the current state of the buffer pool
    void printState() const {
        cout << "Buffer State:" << endl;
        for (int i = 0; i < capacity; ++i) {
            cout << "Frame " << i << ": ";
            if (pool[i].occupied) {
                cout << "Page=" << pool[i].page_id 
                     << " Ref=" << pool[i].ref_count 
                     << " InUse=" << (pool[i].in_use ? "Y" : "N")
                     << (clock_hand == i ? " <- [Hand]" : "") << endl;
            } else {
                cout << "[Empty]" << (clock_hand == i ? " <- [Hand]" : "") << endl;
            }
        }
        cout << "----------------------" << endl;
    }
};

int main() {
    cout << "--- Clock Sweep Page Replacement Algorithm ---" << endl;
    
    // Create a buffer pool of capacity 3
    ClockSweepBuffer buffer(3);
    
    cout << "\n[Action] Inserting pages 10, 20, 30" << endl;
    buffer.put(10);
    buffer.put(20);
    buffer.put(30);
    buffer.printState();
    
    cout << "\n[Action] Accessing pages 10 and 20 (Simulating usage)" << endl;
    buffer.get(10);
    buffer.get(20);
    buffer.printState();
    
    cout << "\n[Action] Releasing pages 10 and 20 (Unpinning)" << endl;
    buffer.release(10);
    buffer.release(20);
    buffer.printState();
    
    cout << "\n[Action] Inserting page 40 (Triggers eviction)" << endl;
    buffer.put(40);
    buffer.printState();
    
    cout << "\n[Action] Inserting page 50 (Triggers eviction)" << endl;
    buffer.put(50);
    buffer.printState();

    return 0;
}
