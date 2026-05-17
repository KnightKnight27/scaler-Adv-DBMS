#include <iostream>
#include <vector>
#include <unordered_map>
#include <iomanip>
#include <stdexcept>
using namespace std;

/*
    Lab 3: Clock Sweep Page Replacement Algorithm

    This program simulates a buffer manager using the Clock Sweep algorithm.
    Each frame stores:
    - pageId
    - reference bit
    - valid bit

    When a page is accessed, its reference bit becomes 1.
    When replacement is needed, the clock hand scans frames:
    - If reference bit is 1, clear it and give a second chance.
    - If reference bit is 0, replace that page.
*/

class ClockSweepBuffer {
private:
    struct Frame {
        int pageId;
        bool referenceBit;
        bool valid;

        Frame() {
            pageId = -1;
            referenceBit = false;
            valid = false;
        }
    };

    vector<Frame> frames;
    unordered_map<int, int> pageToFrame;
    int capacity;
    int clockHand;
    int pageHits;
    int pageFaults;

    void moveClockHand() {
        clockHand = (clockHand + 1) % capacity;
    }

    int findVictimFrame() {
        while (true) {
            Frame &current = frames[clockHand];

            cout << "Clock hand checking frame " << clockHand;

            if (current.referenceBit) {
                cout << " -> page " << current.pageId
                     << " has reference bit 1, giving second chance.\n";
                current.referenceBit = false;
                moveClockHand();
            } else {
                cout << " -> page " << current.pageId
                     << " has reference bit 0, selected for replacement.\n";
                int victimIndex = clockHand;
                moveClockHand();
                return victimIndex;
            }
        }
    }

public:
    ClockSweepBuffer(int size) {
        if (size <= 0) {
            throw invalid_argument("Buffer size must be greater than 0.");
        }

        capacity = size;
        frames.resize(capacity);
        clockHand = 0;
        pageHits = 0;
        pageFaults = 0;
    }

    void accessPage(int pageId) {
        cout << "\nAccess request for page " << pageId << "\n";

        if (pageToFrame.find(pageId) != pageToFrame.end()) {
            int frameIndex = pageToFrame[pageId];
            frames[frameIndex].referenceBit = true;
            pageHits++;

            cout << "Result: Page HIT. Page " << pageId
                 << " already exists in frame " << frameIndex << ".\n";
            cout << "Reference bit is set to 1.\n";
            return;
        }

        pageFaults++;
        cout << "Result: Page MISS. Page " << pageId << " is not in buffer.\n";

        for (int i = 0; i < capacity; i++) {
            if (!frames[i].valid) {
                frames[i].pageId = pageId;
                frames[i].referenceBit = true;
                frames[i].valid = true;
                pageToFrame[pageId] = i;

                cout << "Inserted page " << pageId
                     << " into empty frame " << i << ".\n";
                return;
            }
        }

        cout << "Buffer is full. Clock sweep replacement starts.\n";

        int victimIndex = findVictimFrame();
        int removedPage = frames[victimIndex].pageId;

        pageToFrame.erase(removedPage);

        frames[victimIndex].pageId = pageId;
        frames[victimIndex].referenceBit = true;
        frames[victimIndex].valid = true;
        pageToFrame[pageId] = victimIndex;

        cout << "Evicted page " << removedPage
             << " from frame " << victimIndex << ".\n";
        cout << "Inserted page " << pageId
             << " into frame " << victimIndex << ".\n";
    }

    void displayBuffer() const {
        cout << "\nCurrent Buffer State\n";
        cout << "-----------------------------------------\n";
        cout << left << setw(10) << "Frame"
             << setw(10) << "Page"
             << setw(15) << "Reference"
             << setw(10) << "Valid" << "\n";
        cout << "-----------------------------------------\n";

        for (int i = 0; i < capacity; i++) {
            cout << left << setw(10) << i;

            if (frames[i].valid) {
                cout << setw(10) << frames[i].pageId
                     << setw(15) << frames[i].referenceBit
                     << setw(10) << "Yes";
            } else {
                cout << setw(10) << "-"
                     << setw(15) << "-"
                     << setw(10) << "No";
            }

            if (i == clockHand) {
                cout << " <- clock hand";
            }

            cout << "\n";
        }

        cout << "-----------------------------------------\n";
    }

    void displayStatistics() const {
        int total = pageHits + pageFaults;

        cout << "\nExecution Statistics\n";
        cout << "Total page requests: " << total << "\n";
        cout << "Page hits: " << pageHits << "\n";
        cout << "Page faults: " << pageFaults << "\n";

        if (total > 0) {
            double hitRatio = (double)pageHits / total;
            double faultRatio = (double)pageFaults / total;

            cout << "Hit ratio: " << hitRatio << "\n";
            cout << "Fault ratio: " << faultRatio << "\n";
        }
    }
};

int main() {
    cout << "Clock Sweep Page Replacement Simulation\n";

    int bufferSize;
    cout << "Enter number of buffer frames: ";
    cin >> bufferSize;

    ClockSweepBuffer buffer(bufferSize);

    int n;
    cout << "Enter number of page references: ";
    cin >> n;

    cout << "Enter page reference string:\n";

    for (int i = 0; i < n; i++) {
        int page;
        cin >> page;

        buffer.accessPage(page);
        buffer.displayBuffer();
    }

    buffer.displayStatistics();

    return 0;
}