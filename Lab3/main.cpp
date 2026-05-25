#include <iostream>
#include <unordered_map>
#include <vector>
#include <thread>
#include <chrono>

template<typename T>
class ClockSweep
{
public:
    ClockSweep(int maxNumber)
        : maxCacheSize(maxNumber),
          clockHand(0)
    {
        cache.resize(maxCacheSize);
        bgClockThread = std::thread(&ClockSweep::clockWorker, this);
    }

    ~ClockSweep()
    {
        stopThread = true;

        if(bgClockThread.joinable())
            bgClockThread.join();
    }

    T getKey(T key)
    {
        if(mp.find(key) == mp.end())
        {
            std::cout << "Key not found\n";
            return T();
        }

        int idx = mp[key];

        cache[idx].referenceBit = true;

        std::cout << "Accessed Key : " << key << "\n";

        return cache[idx].key;
    }

    void putKey(T key)
    {
        if(mp.find(key) != mp.end())
        {
            int idx = mp[key];
            cache[idx].referenceBit = true;

            std::cout << "Key already exists : " << key << "\n";
            return;
        }

        while(true)
        {
            if(!cache[clockHand].valid)
            {
                insertAt(clockHand, key);
                return;
            }

            if(cache[clockHand].referenceBit)
            {
                cache[clockHand].referenceBit = false;
            }
            else
            {
                mp.erase(cache[clockHand].key);

                insertAt(clockHand, key);
                return;
            }

            clockHand = (clockHand + 1) % maxCacheSize;
        }
    }

    void display()
    {
        std::cout << "\nCache State:\n";

        for(auto &node : cache)
        {
            if(node.valid)
            {
                std::cout
                    << node.key
                    << " [ref="
                    << node.referenceBit
                    << "]\n";
            }
        }

        std::cout << "\n";
    }

private:

    struct Node
    {
        T key{};
        bool referenceBit{false};
        bool valid{false};
    };

    void insertAt(int idx, T key)
    {
        cache[idx].key = key;
        cache[idx].referenceBit = true;
        cache[idx].valid = true;

        mp[key] = idx;

        std::cout << "Inserted Key : " << key << "\n";

        clockHand = (clockHand + 1) % maxCacheSize;
    }

    void clockWorker()
    {
        while(!stopThread)
        {
            std::this_thread::sleep_for(
                std::chrono::seconds(2)
            );
        }
    }

private:

    uint32_t maxCacheSize{0u};

    int clockHand{0};

    bool stopThread{false};

    std::vector<Node> cache;

    std::unordered_map<T, int> mp;

    std::thread bgClockThread;
};

int main()
{
    ClockSweep<int> clockSweep(3);

    clockSweep.putKey(1);
    clockSweep.putKey(2);
    clockSweep.putKey(3);

    clockSweep.display();

    clockSweep.getKey(1);

    clockSweep.putKey(4);

    clockSweep.display();

    return 0;
}
