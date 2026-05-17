#include <iostream>
#include <vector>
#include <unordered_map>
#include <thread>
#include <mutex>

template<typename T>
class ClockBuffer {
private:
    struct PageFrame {
        T value;
        int referenceBit;
        bool occupied;

        PageFrame() {
            referenceBit = 0;
            occupied = false;
        }
    };

    std::vector<PageFrame> frames;
    std::unordered_map<T, int> pageTable;

    int hand;
    int capacity;

    std::mutex bufferMutex;

public:
    explicit ClockBuffer(int size) {
        capacity = size;
        frames.resize(capacity);
        hand = 0;
    }

    void accessPage(T page) {
        std::lock_guard<std::mutex> lock(bufferMutex);

        auto it = pageTable.find(page);

        if (it != pageTable.end()) {
            int index = it->second;
            frames[index].referenceBit = 1;

            std::cout << "Page "
                      << page
                      << " already exists in buffer\n";

            return;
        }

        insertPage(page);
    }

    void insertPage(T page) {
        while (true) {

            if (!frames[hand].occupied) {
                frames[hand].value = page;
                frames[hand].referenceBit = 1;
                frames[hand].occupied = true;

                pageTable[page] = hand;

                std::cout << "Inserted page "
                          << page
                          << " at frame "
                          << hand
                          << "\n";

                hand = (hand + 1) % capacity;
                return;
            }

            if (frames[hand].referenceBit == 0) {

                pageTable.erase(frames[hand].value);

                std::cout << "Evicting page "
                          << frames[hand].value
                          << "\n";

                frames[hand].value = page;
                frames[hand].referenceBit = 1;

                pageTable[page] = hand;

                std::cout << "Inserted page "
                          << page
                          << " at frame "
                          << hand
                          << "\n";

                hand = (hand + 1) % capacity;
                return;
            }

            frames[hand].referenceBit = 0;

            hand = (hand + 1) % capacity;
        }
    }

    void displayBuffer() {
        std::lock_guard<std::mutex> lock(bufferMutex);
        
        std::cout << "\nCurrent Buffer State\n";

        for (int i = 0; i < capacity; i++) {

            if (frames[i].occupied) {
                std::cout << "Frame "
                          << i
                          << " -> Page "
                          << frames[i].value
                          << " | RefBit: "
                          << frames[i].referenceBit
                          << "\n";
            }
            else {
                std::cout << "Frame "
                          << i
                          << " -> Empty\n";
            }
        }

        std::cout << "\n";
    }
};

int main() {

    ClockBuffer<int> buffer(4);

    buffer.accessPage(1);
    buffer.accessPage(2);
    buffer.accessPage(3);
    buffer.accessPage(4);

    buffer.displayBuffer();

    buffer.accessPage(2);
    buffer.accessPage(1);

    buffer.accessPage(5);

    buffer.displayBuffer();

    buffer.accessPage(6);

    buffer.displayBuffer();

    return 0;
}
