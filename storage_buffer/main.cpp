#include <iostream>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <chrono>
#include <climits>
#include <atomic>

using namespace std;

template<typename T>
class ClockSweep {
public:
    ClockSweep(int maxNumber) : maxCacheSize(maxNumber), running(true) {
        bgClockThread = thread(&ClockSweep::decreaseCounts, this);
    }

    ~ClockSweep() {
        running = false;
        bgClockThread.join();
    }

    T getKey(T key) {
        lock_guard<mutex> lock(mtx);
        if (cache.count(key)) {
            cache[key]++;
            cout << "HIT  key=" << key << " count=" << cache[key] << "\n";
        } else {
            cout << "MISS key=" << key << "\n";
        }
        return key;
    }

    void putKey(T key) {
        lock_guard<mutex> lock(mtx);
        if (cache.count(key)) {
            cache[key]++;
            return;
        }
        if ((int)cache.size() >= maxCacheSize) {
            // evict page with lowest access count
            T victim = cache.begin()->first;
            int minCount = INT_MAX;
            for (auto& [k, v] : cache)
                if (v < minCount) { minCount = v; victim = k; }
            cout << "EVICT key=" << victim << " count=" << minCount << "\n";
            cache.erase(victim);
        }
        cache[key] = 1;
        cout << "LOAD  key=" << key << "\n";
    }

    void print() {
        lock_guard<mutex> lock(mtx);
        cout << "Cache: { ";
        for (auto& [k, v] : cache)
            cout << k << ":" << v << " ";
        cout << "}\n";
    }

private:
    void decreaseCounts() {
        while (running) {
            this_thread::sleep_for(chrono::seconds(1));
            lock_guard<mutex> lock(mtx);
            for (auto& [k, v] : cache)
                if (v > 0) v--;
            cout << "[clock tick] counts decreased\n";
        }
    }

    uint maxCacheSize{0u};
    unordered_map<T, int> cache; 
    thread bgClockThread;
    mutex mtx;
    atomic<bool> running;
};

int main() {
    ClockSweep<int> clockSweep(3);

    clockSweep.putKey(1);
    clockSweep.putKey(2);
    clockSweep.putKey(3);
    clockSweep.print();

    clockSweep.getKey(1);
    clockSweep.getKey(1);
    clockSweep.getKey(2);
    clockSweep.print();

    this_thread::sleep_for(chrono::seconds(2)); // let clock tick twice
    clockSweep.print();

    clockSweep.putKey(4); // should evict 3 (lowest count after ticks)
    clockSweep.print();
}
