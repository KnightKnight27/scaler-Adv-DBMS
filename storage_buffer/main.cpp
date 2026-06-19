#include <iostream>
#include <vector>
#include <unordered_map>
#include <thread>

using namespace std;

template <typename T>
class ClockSweep
{
public:
    ClockSweep(int maxNumber)
    {
        maxCacheSize = maxNumber;
        hand = 0;
        cache.resize(maxCacheSize);
    }

    T getKey(T key)
    {
        auto it = pos.find(key);

        if (it == pos.end())
            return T();

        cache[it->second].ref = true;
        return cache[it->second].key;
    }

    void putKey(T key)
    {
        auto it = pos.find(key);
        if (it != pos.end())
        {
            int idx = it->second;
            cache[idx].ref = true;
            cache[idx].dirty = false;
            return;
        }

        int idx = findSlot();

        if (cache[idx].used)
        {
            pos.erase(cache[idx].key);
        }

        cache[idx].key = key;
        cache[idx].ref = true;
        cache[idx].dirty = false;
        cache[idx].used = true;

        pos[key] = idx;
    }

private:
    struct Entry
    {
        T key{};
        bool ref = false;
        bool dirty = false;  
        bool used = false;   
    };

    int findSlot()
    {
        while (true)
        {
            if (!cache[hand].used)
            {
                int idx = hand;
                moveHand();
                return idx;
            }

            if (!cache[hand].ref)
            {
                int idx = hand;
                moveHand();
                return idx;
            }

            cache[hand].ref = false;
            moveHand();
        }
    }

    void moveHand()
    {
        hand = (hand + 1) % maxCacheSize;
    }

private:
    unsigned int maxCacheSize{0u};
    thread bgClockThread; 

    vector<Entry> cache;
    unordered_map<T, int> pos;
    int hand;
};

int main()
{
    ClockSweep<int> clockSweep(3);

    clockSweep.putKey(1);
    clockSweep.putKey(2);
    clockSweep.putKey(3);

    cout << clockSweep.getKey(2) << endl;

    clockSweep.putKey(4);

    cout << clockSweep.getKey(1) << endl;
    cout << clockSweep.getKey(2) << endl;
    cout << clockSweep.getKey(4) << endl;

}