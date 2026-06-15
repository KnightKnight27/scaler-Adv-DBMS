#include <iostream>
#include <unordered_map>
#include <vector>
using namespace std;

template <typename T>
class ClockSweep {
    struct Frame {
        T key{};
        bool refBit = false;
        bool occupied = false;
    };

    int capacity;
    int hand;
    vector<Frame> buffer;
    unordered_map<T, int> pageTable;

    void evictAndInsert(T key) {
        while (true) {
            if (!buffer[hand].occupied) {
                placeAt(hand, key);
                return;
            }
            if (buffer[hand].refBit) {
                buffer[hand].refBit = false;
            } else {
                cout << "Evicted: " << buffer[hand].key << endl;
                pageTable.erase(buffer[hand].key);
                placeAt(hand, key);
                return;
            }
            hand = (hand + 1) % capacity;
        }
    }

    void placeAt(int idx, T key) {
        buffer[idx] = {key, true, true};
        pageTable[key] = idx;
        hand = (idx + 1) % capacity;
    }

public:
    ClockSweep(int size) : capacity(size), hand(0), buffer(size) {}

    void put(T key) {
        if (pageTable.find(key) != pageTable.end()) {
            buffer[pageTable[key]].refBit = true;
            cout << "Already in buffer: " << key << endl;
            return;
        }
        cout << "Inserting: " << key << endl;
        evictAndInsert(key);
    }

    bool get(T key) {
        if (pageTable.find(key) == pageTable.end()) {
            cout << "Miss: " << key << endl;
            return false;
        }
        buffer[pageTable[key]].refBit = true;
        cout << "Hit: " << key << endl;
        return true;
    }

    void display() {
        cout << "\n--- Buffer State (hand at " << hand << ") ---" << endl;
        for (int i = 0; i < capacity; i++) {
            if (buffer[i].occupied)
                cout << "  [" << i << "] " << buffer[i].key
                     << " ref=" << buffer[i].refBit
                     << (i == hand ? " <-- hand" : "") << endl;
            else
                cout << "  [" << i << "] empty"
                     << (i == hand ? " <-- hand" : "") << endl;
        }
        cout << endl;
    }
};

int main() {
    cout << "=== Clock Sweep Buffer Manager ===" << endl;
    ClockSweep<int> buf(3);

    buf.put(10);
    buf.put(20);
    buf.put(30);
    buf.display();

    buf.get(10);

    buf.put(40);
    buf.display();

    buf.put(50);
    buf.display();

    buf.get(20);
    buf.get(10);

    return 0;
}
