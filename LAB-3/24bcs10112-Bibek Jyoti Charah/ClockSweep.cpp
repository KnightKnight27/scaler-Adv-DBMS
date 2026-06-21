#include <iostream>
#include <vector>
#include <fstream>


template <typename T, typename U>
class ClockNode {
    public:
        T key;
        U value;
        int usageCount;
        bool valid;

        ClockNode() : key(), value(), usageCount(0), valid(false) {}
};

template <typename T, typename U>
class ClockSweep {
public:
    ClockSweep(size_t capacity) : capacity(capacity) {
        buffer.resize(capacity);
        clockHand = 0;
    }

    U get(T key) {
        for (int i = 0; i < capacity; i++) {
            if (buffer[i].valid && buffer[i].key == key) {
                buffer[i].usageCount++;
                return buffer[i].value;
            }
        }
        return U();
    }

    void put(T key, U value) {
        while (true) {
            ClockNode<T, U>& node = buffer[clockHand];
            if (node.valid && node.key == key) {
                node.value = value;
                node.usageCount++;
                break;
            }
            else if (!node.valid || node.usageCount == 0) {
                node.key = key;
                node.value = value;
                node.usageCount = 1;
                node.valid = true;
                break;
            } else {
                node.usageCount--;
            }
            clockHand = (clockHand + 1) % capacity;
        }
        clockHand = (clockHand + 1) % capacity;

    }

    void display() {
        std::cout << "Cache State:\n";
        for (int i = 0; i < capacity; i++) {
            if (buffer[i].valid) {
                std::cout << "[" << buffer[i].key
                    << ":" << buffer[i].value
                    << " UC:" << buffer[i].usageCount << "] ";
            } else {
                std::cout << "[Empty] ";
            }
        }
        std::cout << std::endl;
    }

private:
    size_t capacity{0};
    std::vector<ClockNode<T, U>> buffer;
    int clockHand;
};


int main() {
    int t, k, v, size;
    std::ifstream infile("input.txt");
    infile >> size;
    ClockSweep<int, int> clock(size);
    while (infile >> t >> k >> v) {
        if (t == 1) {
            clock.put(k, v);
            std::cout << "Put: Key=" << k << ", Value=" << v << std::endl;
        } else if (t == 2) {
            int value = clock.get(k);
            if (value != 0) {
                std::cout << "Get: Key=" << k << ", Value=" << value << std::endl;
            } else {
                std::cout << "Get: Key=" << k << " not found in cache." << std::endl;
            }
        }
        clock.display();
        std::cout << "-----------------------------" << std::endl;
    }

    return 0;
}