#include <iostream>
#include <vector>
#include <thread>
#include <chrono>

using namespace std;

template<typename T>
struct Frame {

    T key;
    bool refBit;

    Frame(T k) {
        key = k;
        refBit = true;
    }
};

template<typename T>
class ClockSweep {

private:

    vector<Frame<T>*> buffer;

    int capacity;
    int hand;

    bool running;

    thread sweeperThread;

public:

    ClockSweep(int cap) {

        capacity = cap;
        hand = 0;

        running = true;

        sweeperThread =
            thread(&ClockSweep::sweeper, this);
    }

    void sweeper() {

        while (running) {

            cout << "Background sweep running... hand="
                 << hand
                 << endl;

            this_thread::sleep_for(
                chrono::seconds(1)
            );
        }
    }

    void put(T key) {

        for (auto frame : buffer) {

            if (frame->key == key) {

                frame->refBit = true;

                return;
            }
        }

        if (buffer.size() < capacity) {

            buffer.push_back(
                new Frame<T>(key)
            );

            return;
        }

        while (true) {

            if (!buffer[hand]->refBit) {

                delete buffer[hand];

                buffer[hand] =
                    new Frame<T>(key);

                hand =
                    (hand + 1) % capacity;

                return;
            }

            buffer[hand]->refBit = false;

            hand =
                (hand + 1) % capacity;
        }
    }

    bool get(T key) {

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

            cout
                << "["
                << frame->key
                << " ref="
                << frame->refBit
                << "] ";
        }

        cout << endl;
    }

    ~ClockSweep() {

        running = false;

        sweeperThread.join();

        for (auto frame : buffer) {
            delete frame;
        }
    }
};

int main() {

    ClockSweep<string> cache(3);

    cache.put("Page1");
    cache.put("Page2");
    cache.put("Page3");

    cache.display();

    cache.get("Page1");

    cache.put("Page4");

    cache.display();

    this_thread::sleep_for(
        chrono::seconds(5)
    );

    return 0;
}