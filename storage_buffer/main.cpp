#include <iostream>
#include <vector>
#include <map>
#include <thread>

using namespace std;

template<typename T>
class ClockSweep
{
private:

    struct Page
    {
        T key;
        bool referenceBit;
        bool dirtyBit;
        bool occupied;

        Page()
        {
            referenceBit = false;
            dirtyBit = false;
            occupied = false;
        }
    };

    vector<Page> frames;
    map<T, int> pageIndex;

    int maxCacheSize{0u};
    int hand{0};

    thread bgClockThread;

public:

    ClockSweep(int maxNumber)
    {
        maxCacheSize = maxNumber;
        frames.resize(maxCacheSize);
    }

    // Read operation
    T getKey(T key)
    {
        if (pageIndex.find(key) != pageIndex.end())
        {
            int idx = pageIndex[key];
            frames[idx].referenceBit = true;

            cout << "Page Hit : " << key << endl;
            return key;
        }

        cout << "Page Miss : " << key << endl;
        insertPage(key, false);

        return key;
    }

    // Write operation
    void putKey(T key)
    {
        if (pageIndex.find(key) != pageIndex.end())
        {
            int idx = pageIndex[key];

            frames[idx].referenceBit = true;
            frames[idx].dirtyBit = true;

            cout << "Updated Page : " << key << endl;
            return;
        }

        cout << "Inserted Page : " << key << endl;
        insertPage(key, true);
    }

private:

    void insertPage(T key, bool dirty)
    {
        int victim = findVictim();

        if (victim == -1)
        {
            cout << "No space available" << endl;
            return;
        }

        // Remove previous page
        if (frames[victim].occupied)
        {
            T oldKey = frames[victim].key;
            pageIndex.erase(oldKey);
        }

        // Insert new page
        frames[victim].key = key;
        frames[victim].referenceBit = true;
        frames[victim].dirtyBit = dirty;
        frames[victim].occupied = true;

        pageIndex[key] = victim;
    }

    int findVictim()
    {
        while (true)
        {
            // Empty frame found
            if (!frames[hand].occupied)
            {
                int pos = hand;
                hand = (hand + 1) % maxCacheSize;
                return pos;
            }

            // Replace frame
            if (!frames[hand].referenceBit)
            {
                int pos = hand;
                hand = (hand + 1) % maxCacheSize;
                return pos;
            }

            // Second chance
            frames[hand].referenceBit = false;
            hand = (hand + 1) % maxCacheSize;
        }

        return -1;
    }
};

int main()
{
    ClockSweep<int> clockSweep(3);

    clockSweep.putKey(1);
    clockSweep.putKey(2);
    clockSweep.putKey(3);

    clockSweep.getKey(1);

    clockSweep.putKey(4);

    return 0;
}