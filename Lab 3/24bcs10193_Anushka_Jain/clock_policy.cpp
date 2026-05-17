#include <iostream>
#include <vector>
#include <unordered_map>

using namespace std;

class SecondChanceCache {

private:

    struct Slot {
        int page = -1;
        bool refBit = false;
        bool occupied = false;
    };

    vector<Slot> memory;
    unordered_map<int, int> pageTable;

    int hand = 0;
    int capacity;

public:

    SecondChanceCache(int size) {

        capacity = size;
        memory.resize(size);
    }

    void insertPage(int page) {

        // Page already exists
        if (pageTable.count(page)) {

            int index = pageTable[page];
            memory[index].refBit = true;

            cout << "Page "
                 << page
                 << " accessed again -> Reference bit updated\n";

            return;
        }

        while (true) {

            // Empty frame found
            if (!memory[hand].occupied) {

                addPage(page);
                return;
            }

            // Replace if reference bit is false
            if (memory[hand].refBit == false) {

                cout << "Replacing page "
                     << memory[hand].page
                     << " with page "
                     << page
                     << endl;

                pageTable.erase(memory[hand].page);

                addPage(page);
                return;
            }

            // Give second chance
            cout << "Second chance given to page "
                 << memory[hand].page
                 << endl;

            memory[hand].refBit = false;

            moveHand();
        }
    }

    void addPage(int page) {

        memory[hand].page = page;
        memory[hand].occupied = true;
        memory[hand].refBit = true;

        pageTable[page] = hand;

        moveHand();
    }

    void moveHand() {

        hand = (hand + 1) % capacity;
    }

    void displayFrames() {

        cout << "\nCurrent Memory State:\n";

        for (int i = 0; i < capacity; i++) {

            if (!memory[i].occupied) {

                cout << "Frame "
                     << i
                     << " -> Empty\n";
            }

            else {

                cout << "Frame "
                     << i
                     << " -> Page: "
                     << memory[i].page
                     << " | RefBit: "
                     << memory[i].refBit
                     << endl;
            }
        }

        cout << endl;
    }
};

int main() {

    cout << "Clock Sweep / Second Chance Algorithm Demo\n\n";

    SecondChanceCache cache(3);

    cache.insertPage(1);
    cache.insertPage(2);
    cache.insertPage(3);

    cache.displayFrames();

    cache.insertPage(1);

    cache.insertPage(4);

    cache.displayFrames();

    cache.insertPage(2);

    cache.insertPage(5);

    cache.displayFrames();

    return 0;
}