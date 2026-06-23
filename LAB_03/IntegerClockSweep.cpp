#include <iostream>
#include <vector>

using namespace std;

struct Frame {
    int key;
    bool refBit;

    Frame(int k) {
        key = k;
        refBit = true;
    }
};

class ClockSweep {

private:
    vector<Frame*> buffer;
    int capacity;
    int hand;

public:

    ClockSweep(int cap) {
        capacity = cap;
        hand = 0;
    }

    void put(int key) {

        for (auto frame : buffer) {

            if (frame->key == key) {
                frame->refBit = true;
                return;
            }
        }

        if (buffer.size() < capacity) {
            buffer.push_back(new Frame(key));
            return;
        }

        while (true) {

            if (!buffer[hand]->refBit) {

                delete buffer[hand];

                buffer[hand] = new Frame(key);

                hand = (hand + 1) % capacity;

                return;
            }

            buffer[hand]->refBit = false;

            hand = (hand + 1) % capacity;
        }
    }

    bool get(int key) {

        for (auto frame : buffer) {

            if (frame->key == key) {

                frame->refBit = true;

                return true;
            }
        }

        return false;
    }

    void display() {

        cout << "\nBuffer State:\n";

        for (auto frame : buffer) {

            cout << "["
                 << frame->key
                 << " ref="
                 << frame->refBit
                 << "] ";
        }

        cout << endl;
    }
};

int main() {

    ClockSweep cache(3);

    cache.put(1);
    cache.put(2);
    cache.put(3);

    cache.display();

    cache.get(1);

    cache.put(4);

    cache.display();

    return 0;
}