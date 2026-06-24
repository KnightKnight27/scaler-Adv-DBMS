#include <iostream>
#include <vector>
#include <unordered_map>
#include <algorithm>

using namespace std;

struct BufferFrame {
    int pageId;
    int usageCount;
    bool pinned;

    BufferFrame() {
        pageId = -1;
        usageCount = 0;
        pinned = false;
    }
};

class ClockBufferPool {
private:
    vector<BufferFrame> frames;
    unordered_map<int, int> pageMap;
    int handPosition;
    int poolSize;

public:
    ClockBufferPool(int size) {
        poolSize = size;
        frames.resize(size);
        handPosition = 0;
    }

    int selectVictim() {
        int scans = 0;

        while (scans < poolSize * 2) {
            BufferFrame &frame = frames[handPosition];

            if (!frame.pinned) {
                if (frame.usageCount == 0) {
                    int victim = handPosition;
                    handPosition = (handPosition + 1) % poolSize;
                    return victim;
                }

                frame.usageCount--;
            }

            handPosition = (handPosition + 1) % poolSize;
            scans++;
        }

        return -1;
    }

    void accessPage(int pageId) {

        if (pageMap.find(pageId) != pageMap.end()) {
            int index = pageMap[pageId];

            frames[index].usageCount =
                min(frames[index].usageCount + 1, 5);

            cout << "HIT  -> Page "
                 << pageId
                 << " found in Frame "
                 << index
                 << " (usage="
                 << frames[index].usageCount
                 << ")" << endl;

            return;
        }

        int victim = selectVictim();

        if (victim == -1) {
            cout << "No frame available for replacement." << endl;
            return;
        }

        if (frames[victim].pageId != -1) {
            cout << "EVICT -> Removing Page "
                 << frames[victim].pageId
                 << " from Frame "
                 << victim
                 << endl;

            pageMap.erase(frames[victim].pageId);
        }

        frames[victim].pageId = pageId;
        frames[victim].usageCount = 1;
        frames[victim].pinned = false;

        pageMap[pageId] = victim;

        cout << "MISS -> Loaded Page "
             << pageId
             << " into Frame "
             << victim
             << endl;
    }

    void displayBuffer() {
        cout << "\nCurrent Buffer Pool State\n";
        cout << "---------------------------------\n";

        for (int i = 0; i < poolSize; i++) {
            cout << "Frame " << i
                 << " | Page: ";

            if (frames[i].pageId == -1)
                cout << "--";
            else
                cout << frames[i].pageId;

            cout << " | Usage Count: "
                 << frames[i].usageCount;

            if (i == handPosition)
                cout << " <-- Clock Hand";

            cout << endl;
        }

        cout << "---------------------------------\n";
    }
};

int main() {

    ClockBufferPool buffer(4);

    vector<int> pageRequests =
        {1, 2, 3, 4, 1, 2, 5, 1, 2, 3, 4, 5};

    cout << "Clock Sweep Page Replacement Simulation\n\n";

    for (int page : pageRequests) {
        buffer.accessPage(page);
    }

    buffer.displayBuffer();

    return 0;
}