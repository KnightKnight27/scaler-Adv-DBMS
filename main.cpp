#include <iostream>
#include <vector>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <condition_variable>

using namespace std;

template <typename T>
class ClockSweep {
    struct Frame {
        T key;
        bool ref;
        bool used;
    };

    unsigned int maxCacheSize = 0;
    vector<Frame> frames;
    unordered_map<T, int> lookup;
    int hand = 0;
    int filled = 0;

    mutex mu;
    condition_variable needEvict, didEvict;
    bool wantEvict = false;
    bool stop = false;

    thread evictionClockThread;

    void runClock() {
        unique_lock<mutex> lk(mu);
        while (true) {
            needEvict.wait(lk, [&]{ return wantEvict || stop; });
            if (stop) return;

            for (;;) {
                Frame& f = frames[hand];
                if (f.used) {
                    if (f.ref) {
                        f.ref = false;
                    } else {
                        lookup.erase(f.key);
                        f.used = false;
                        filled--;
                        hand = (hand + 1) % maxCacheSize;
                        break;
                    }
                }
                hand = (hand + 1) % maxCacheSize;
            }

            wantEvict = false;
            didEvict.notify_one();
        }
    }

public:
    ClockSweep(unsigned int size) : maxCacheSize(size), frames(size) {
        for (auto& f : frames) { f.ref = false; f.used = false; }
        evictionClockThread = thread(&ClockSweep::runClock, this);
    }

    ~ClockSweep() {
        {
            lock_guard<mutex> lk(mu);
            stop = true;
        }
        needEvict.notify_all();
        evictionClockThread.join();
    }

    T get(T key) {
        lock_guard<mutex> lk(mu);
        auto it = lookup.find(key);
        if (it == lookup.end()) return T();
        frames[it->second].ref = true;
        return frames[it->second].key;
    }

    void put(T key) {
        unique_lock<mutex> lk(mu);
        auto it = lookup.find(key);
        if (it != lookup.end()) {
            frames[it->second].ref = true;
            return;
        }

        if ((unsigned)filled >= maxCacheSize) {
            wantEvict = true;
            needEvict.notify_one();
            didEvict.wait(lk, [&]{ return !wantEvict; });
        }

        // grab the first free frame
        int slot = -1;
        for (int i = 0; i < (int)frames.size(); i++) {
            if (!frames[i].used) { slot = i; break; }
        }
        frames[slot].key = key;
        frames[slot].ref = true;
        frames[slot].used = true;
        lookup[key] = slot;
        filled++;
    }
};

int main() {
    ClockSweep<int> c(3);
    c.put(1);
    c.put(2);
    c.put(3);
    c.put(4); // evicts 1

    cout << "int cache after put(1..4):\n";
    cout << " 1 -> " << c.get(1) << "\n";
    cout << " 2 -> " << c.get(2) << "\n";
    cout << " 3 -> " << c.get(3) << "\n";
    cout << " 4 -> " << c.get(4) << "\n";

    c.put(5);
    cout << "after put(5):\n";
    cout << " 2 -> " << c.get(2) << "\n";
    cout << " 3 -> " << c.get(3) << "\n";
    cout << " 4 -> " << c.get(4) << "\n";
    cout << " 5 -> " << c.get(5) << "\n";

    ClockSweep<string> s(2);
    s.put("a");
    s.put("b");
    s.put("c");
    cout << "\nstring cache:\n";
    cout << " a -> '" << s.get("a") << "'\n";
    cout << " b -> '" << s.get("b") << "'\n";
    cout << " c -> '" << s.get("c") << "'\n";

    return 0;
}
