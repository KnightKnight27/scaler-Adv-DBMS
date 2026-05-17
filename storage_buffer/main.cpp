#include <iostream>
#include <vector>
#include <unordered_map>
#include <mutex>

using namespace std;

template <typename T>
class BufferManager {

private:

    struct Frame {

        T pageId;
        bool reference;
        bool valid;

        Frame() {
            reference = false;
            valid = false;
        }
    };

    vector<Frame> bufferPool;
    unordered_map<T, int> pageIndexMap;

    int pointer;
    int maxSize;

    mutex mtx;

public:

    BufferManager(int size) {

        maxSize = size;
        pointer = 0;

        bufferPool.resize(maxSize);
    }

    void requestPage(T page) {

        lock_guard<mutex> lock(mtx);

        if (pageIndexMap.find(page) != pageIndexMap.end()) {

            int idx = pageIndexMap[page];

            bufferPool[idx].reference = true;

            cout << "[HIT] Page "
                 << page
                 << " found in frame "
                 << idx
                 << endl;

            return;
        }

        cout << "[MISS] Page "
             << page
             << " not found\n";

        replacePage(page);
    }

    void replacePage(T page) {

        while (true) {

            if (!bufferPool[pointer].valid) {

                insertIntoFrame(page, pointer);
                advancePointer();

                return;
            }

            if (bufferPool[pointer].reference == false) {

                cout << "Evicting Page "
                     << bufferPool[pointer].pageId
                     << " from frame "
                     << pointer
                     << endl;

                pageIndexMap.erase(bufferPool[pointer].pageId);

                insertIntoFrame(page, pointer);

                advancePointer();

                return;
            }

            bufferPool[pointer].reference = false;

            advancePointer();
        }
    }

    void insertIntoFrame(T page, int index) {

        bufferPool[index].pageId = page;
        bufferPool[index].reference = true;
        bufferPool[index].valid = true;

        pageIndexMap[page] = index;

        cout << "Inserted Page "
             << page
             << " into frame "
             << index
             << endl;
    }

    void advancePointer() {

        pointer = (pointer + 1) % maxSize;
    }

    void printBufferState() {

        cout << "\n========== BUFFER STATE ==========\n";

        for (int i = 0; i < maxSize; i++) {

            cout << "Frame "
                 << i
                 << " : ";

            if (bufferPool[i].valid) {

                cout << "Page "
                     << bufferPool[i].pageId
                     << " | RefBit = "
                     << bufferPool[i].reference;
            }

            else {

                cout << "Empty";
            }

            cout << endl;
        }

        cout << "==================================\n\n";
    }
};

int main() {

    BufferManager<int> manager(4);

    manager.requestPage(10);
    manager.requestPage(20);
    manager.requestPage(30);
    manager.requestPage(40);

    manager.printBufferState();

    manager.requestPage(20);
    manager.requestPage(10);

    manager.requestPage(50);

    manager.printBufferState();

    manager.requestPage(60);

    manager.printBufferState();

    return 0;
}
