#include <iostream>
#include <unordered_map>
#include <vector>
#include <thread>
#include <chrono>

using namespace std;

template <class T>
class SecondChanceCache
{
private:

    struct CacheEntry
    {
        T value;
        bool used;
        bool occupied;

        CacheEntry()
        {
            used = false;
            occupied = false;
        }
    };

    vector<CacheEntry> pages;

    unordered_map<T, int> positionMap;

    int capacity;
    int pointerIndex;

    bool terminateThread = false;

    thread workerThread;

private:

    void addElement(int pos, T value)
    {
        pages[pos].value = value;
        pages[pos].used = true;
        pages[pos].occupied = true;

        positionMap[value] = pos;

        cout << "Inserted : " << value << endl;

        pointerIndex = (pointerIndex + 1) % capacity;
    }

    void backgroundProcess()
    {
        while(!terminateThread)
        {
            this_thread::sleep_for(
                chrono::milliseconds(1500)
            );
        }
    }

public:

    SecondChanceCache(int size)
    {
        capacity = size;
        pointerIndex = 0;

        pages.resize(capacity);

        workerThread = thread(
            &SecondChanceCache::backgroundProcess,
            this
        );
    }

    ~SecondChanceCache()
    {
        terminateThread = true;

        if(workerThread.joinable())
        {
            workerThread.join();
        }
    }

    void insert(T value)
    {
        if(positionMap.count(value))
        {
            int idx = positionMap[value];

            pages[idx].used = true;

            cout << "Already Present : "
                 << value << endl;

            return;
        }

        while(true)
        {
            if(!pages[pointerIndex].occupied)
            {
                addElement(pointerIndex, value);
                return;
            }

            if(pages[pointerIndex].used)
            {
                pages[pointerIndex].used = false;
            }
            else
            {
                positionMap.erase(
                    pages[pointerIndex].value
                );

                addElement(pointerIndex, value);
                return;
            }

            pointerIndex =
                (pointerIndex + 1) % capacity;
        }
    }

    T access(T value)
    {
        if(!positionMap.count(value))
        {
            cout << "Element not found\n";
            return T();
        }

        int idx = positionMap[value];

        pages[idx].used = true;

        cout << "Accessed : "
             << value << endl;

        return pages[idx].value;
    }

    void printCache()
    {
        cout << "\nCurrent Cache:\n";

        for(auto &item : pages)
        {
            if(item.occupied)
            {
                cout
                    << item.value
                    << " (bit="
                    << item.used
                    << ")\n";
            }
        }

        cout << endl;
    }
};

int main()
{
    SecondChanceCache<int> cache(3);

    cache.insert(10);
    cache.insert(20);
    cache.insert(30);

    cache.printCache();

    cache.access(10);

    cache.insert(40);

    cache.printCache();

    return 0;
}