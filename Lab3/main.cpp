#include <iostream>
#include <unordered_map>
#include <vector>
#include <thread>
#include <chrono>

template <typename T>
class CircularCacheManager
{
public:
    CircularCacheManager(int capacity)
        : cacheLimit(capacity), pointer(0)
    {
        slots.resize(cacheLimit);

        workerThread = std::thread(
            &CircularCacheManager::backgroundTask,
            this
        );
    }

    ~CircularCacheManager()
    {
        terminate = true;

        if (workerThread.joinable())
        {
            workerThread.join();
        }
    }

    // Retrieve an item from cache
    T fetch(T value)
    {
        auto it = indexTable.find(value);

        if (it == indexTable.end())
        {
            std::cout << "Requested key missing.\n";
            return T{};
        }

        int pos = it->second;

        slots[pos].recentlyUsed = true;

        std::cout << "Retrieved: " << value << "\n";

        return slots[pos].data;
    }

    // Insert an item into cache
    void store(T value)
    {
        auto existing = indexTable.find(value);

        // If already exists, just refresh reference bit
        if (existing != indexTable.end())
        {
            int pos = existing->second;
            slots[pos].recentlyUsed = true;

            std::cout << "Already in cache: "
                      << value << "\n";
            return;
        }

        // Clock replacement logic
        while (true)
        {
            CacheEntry& current = slots[pointer];

            if (!current.active)
            {
                placeEntry(pointer, value);
                return;
            }

            if (current.recentlyUsed)
            {
                current.recentlyUsed = false;
            }
            else
            {
                indexTable.erase(current.data);
                placeEntry(pointer, value);
                return;
            }

            advancePointer();
        }
    }

    void printCache()
    {
        std::cout << "\n===== CACHE CONTENT =====\n";

        for (const auto& item : slots)
        {
            if (item.active)
            {
                std::cout
                    << "Value: " << item.data
                    << " | RefBit: "
                    << item.recentlyUsed
                    << "\n";
            }
        }

        std::cout << "=========================\n\n";
    }

private:
    struct CacheEntry
    {
        T data{};
        bool recentlyUsed = false;
        bool active = false;
    };

    void placeEntry(int index, T value)
    {
        slots[index].data = value;
        slots[index].recentlyUsed = true;
        slots[index].active = true;

        indexTable[value] = index;

        std::cout
            << "Inserted: "
            << value
            << "\n";

        advancePointer();
    }

    void advancePointer()
    {
        pointer = (pointer + 1) % cacheLimit;
    }

    // Background thread (dummy worker)
    void backgroundTask()
    {
        while (!terminate)
        {
            std::this_thread::sleep_for(
                std::chrono::seconds(2)
            );
        }
    }

private:
    int cacheLimit;
    int pointer;

    bool terminate = false;

    std::vector<CacheEntry> slots;
    std::unordered_map<T, int> indexTable;

    std::thread workerThread;
};

int main()
{
    CircularCacheManager<int> cache(3);
    // Example 
    cache.store(1);
    cache.store(2);
    cache.store(3);

    cache.printCache();

    cache.fetch(1);

    cache.store(4);

    cache.printCache();

    return 0;
}