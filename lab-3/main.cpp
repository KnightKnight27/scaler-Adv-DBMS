#include <iostream>
#include <vector>

using namespace std;

struct CacheBlock
{
    int key;
    int value;
    int useCount;
    bool isFilled;
};

class ClockSweepCache
{
private:
    vector<CacheBlock> cache;
    int cacheSize;
    int clockPointer;

    void movePointer()
    {
        clockPointer = (clockPointer + 1) % cacheSize;
    }

public:
    ClockSweepCache(int size)
    {
        cacheSize = size;
        clockPointer = 0;

        for (int i = 0; i < cacheSize; i++)
        {
            CacheBlock block;
            block.key = -1;
            block.value = -1;
            block.useCount = 0;
            block.isFilled = false;

            cache.push_back(block);
        }
    }

    void insert(int key, int value)
    {
        while (true)
        {
            if (!cache[clockPointer].isFilled)
            {
                cache[clockPointer].key = key;
                cache[clockPointer].value = value;
                cache[clockPointer].useCount = 1;
                cache[clockPointer].isFilled = true;

                movePointer();
                break;
            }

            if (cache[clockPointer].useCount == 0)
            {
                cache[clockPointer].key = key;
                cache[clockPointer].value = value;
                cache[clockPointer].useCount = 1;

                movePointer();
                break;
            }

            // Give this block one more chance before replacing it.
            cache[clockPointer].useCount--;
            movePointer();
        }
    }

    bool search(int key, int &value)
    {
        for (int i = 0; i < cacheSize; i++)
        {
            if (cache[i].isFilled && cache[i].key == key)
            {
                cache[i].useCount++;
                value = cache[i].value;
                return true;
            }
        }

        return false;
    }

    void display()
    {
        cout << "\nCache State:\n";

        for (int i = 0; i < cacheSize; i++)
        {
            cout << "[" << i << "] ";

            if (cache[i].isFilled)
            {
                cout << "Key=" << cache[i].key
                     << " Value=" << cache[i].value
                     << " Usage=" << cache[i].useCount;
            }
            else
            {
                cout << "EMPTY";
            }

            if (i == clockPointer)
            {
                cout << " <-- clock";
            }

            cout << endl;
        }
    }
};

int main()
{
    ClockSweepCache cache(3);

    cache.insert(1, 100);
    cache.insert(2, 200);
    cache.insert(3, 300);

    cache.display();

    int value;

    if (cache.search(1, value))
    {
        cout << "\nFound value: " << value << endl;
    }
    else
    {
        cout << "\nValue not found" << endl;
    }

    cache.insert(4, 400);

    cache.display();

    return 0;
}
