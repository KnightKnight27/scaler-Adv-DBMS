#include <unordered_map>
#include <iostream>
#include <thread>
#include <mutex>
#include <chrono>
#include <atomic>

using namespace std;

// Linear Decay Cache:
// Each key has a frequency counter. A background
// thread periodically decrements all counters by 1.
// When inserting into a full cache, the key with
// the lowest counter is evicted. Frequently accessed
// keys accumulate high counters and survive decay rounds.

template <typename T>
class LinearDecay
{
public:
    LinearDecay(int maxSize) : maxSize(maxSize), decayStatus(true)
    {
        decayThread = thread(&LinearDecay::decay, this);
    }

    ~LinearDecay()
    {
        decayStatus = false;
        if (decayThread.joinable())
            decayThread.join();
    }

    T get(T key)
    {
        lock_guard<mutex> lock(mtx);
        if (cache.find(key) != cache.end())
        {
            cache[key]++;
            return key;
        }
        cout << "Key not found in cache. Might have been evicted." << endl;
        return T();
    }

    void put(T key)
    {
        lock_guard<mutex> lock(mtx);
        cache[key]++;
        if ((int)cache.size() > maxSize)
            evict();
    }

private:
    int                    maxSize;
    unordered_map<T, int>  cache;
    thread                 decayThread;
    atomic<bool>           decayStatus;
    mutex                  mtx;

    void evict()
    {
        auto minIt = cache.begin();
        for (auto it = cache.begin(); it != cache.end(); it++)
            if (it->second < minIt->second) minIt = it;
        cout << "Evicting key: " << minIt->first
             << " (count=" << minIt->second << ")" << endl;
        cache.erase(minIt);
    }

    void decay()
    {
        while (decayStatus)
        {
            this_thread::sleep_for(chrono::milliseconds(500));
            lock_guard<mutex> lock(mtx);
            for (auto& [key, count] : cache)
            {
                if (count > 0) count--;
            }
        }
    }
};

int main()
{
    cout << "=== Linear Decay Cache Demo ===" << endl;

    LinearDecay<int> cache(3);

    // populate
    cache.put(1);
    cache.put(2);
    cache.put(3);

    // access key 1 frequently — it accumulates a higher count
    cache.get(1);
    cache.get(1);
    cache.get(1);
    cache.get(2);

    cout << "\nCache full (size 3). Inserting key 4 -> should evict least-accessed key." << endl;
    cache.put(4);

    cout << "\nLooking up keys:" << endl;
    cache.get(1);
    cache.get(2);
    cache.get(3);
    cache.get(4);

    cout << "\nWaiting for decay round (1s)..." << endl;
    this_thread::sleep_for(chrono::milliseconds(1100));

    cout << "\nAfter decay — counters decremented by 1. Inserting key 5:" << endl;
    cache.put(5);

    cout << "\nFinal lookups:" << endl;
    cache.get(1);
    cache.get(4);
    cache.get(5);

    return 0;
}
