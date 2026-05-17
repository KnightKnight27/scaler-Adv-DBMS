#include <iostream>
#include <vector>

using namespace std;

class ClockSweep {
private:
    vector<int> pages;
    vector<bool> refBit;

    int capacity;
    int hand;

public:
    ClockSweep(int size) {
        capacity = size;
        hand = 0;

        pages.resize(capacity, -1);
        refBit.resize(capacity, false);
    }

    void accessPage(int page) {

        // Check for page hit
        for (int i = 0; i < capacity; i++) {
            if (pages[i] == page) {
                refBit[i] = true;

                cout << "Page " << page << " -> HIT\n";
                return;
            }
        }

        // Page fault
        cout << "Page " << page << " -> FAULT\n";

        while (true) {

            // Empty frame
            if (pages[hand] == -1) {
                pages[hand] = page;
                refBit[hand] = true;

                hand = (hand + 1) % capacity;
                break;
            }

            // Replace if reference bit = 0
            if (!refBit[hand]) {
                pages[hand] = page;
                refBit[hand] = true;

                hand = (hand + 1) % capacity;
                break;
            }

            // Give second chance
            refBit[hand] = false;
            hand = (hand + 1) % capacity;
        }
    }

    void displayFrames() {
        cout << "Frames: ";

        for (int i = 0; i < capacity; i++) {
            if (pages[i] == -1)
                cout << "[ ] ";
            else
                cout << "[" << pages[i] << "] ";
        }

        cout << "\n";
    }
};

int main() {

    int frameSize = 3;

    vector<int> pageRequests = {
        1, 2, 3, 2, 4, 1, 5, 2, 1, 6
    };

    ClockSweep clock(frameSize);

    for (int page : pageRequests) {
        clock.accessPage(page);
        clock.displayFrames();

        cout << "----------------------\n";
    }

    return 0;
}