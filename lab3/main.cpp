#include <iostream>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <optional>

template <typename T>
class ClockSweep
{
public:
    struct CacheRecord
    {
        T key;
        bool referenceBit;
    };

    explicit ClockSweep(size_t maxSize)
        : maxCacheSize(maxSize),
          clockHand(0)
    {
    }

    std::optional<CacheRecord> getKey(const T &key)
    {
        std::lock_guard<std::mutex> lock(cacheMutex);

        auto it = keyToIndex.find(key);

        if (it == keyToIndex.end())
        {
            std::cout << "Key " << key << " not found\n";
            return std::nullopt;
        }

        clockCache[it->second].referenceBit = true;

        std::cout << "Accessed key "
                  << key
                  << " -> reference bit set to 1\n";

        return clockCache[it->second];
    }

    void putKey(const T &key)
    {
        std::lock_guard<std::mutex> lock(cacheMutex);

        auto existing = keyToIndex.find(key);

        if (existing != keyToIndex.end())
        {
            clockCache[existing->second].referenceBit = true;

            std::cout
                << "Key already exists. Reference bit refreshed.\n";

            return;
        }

        if (clockCache.size() < maxCacheSize)
        {
            clockCache.push_back({key, true});

            keyToIndex[key] = clockCache.size() - 1;

            std::cout
                << "Inserted key "
                << key
                << "\n";

            return;
        }

        while (true)
        {
            if (clockCache[clockHand].referenceBit)
            {
                std::cout
                    << "Second chance given to key "
                    << clockCache[clockHand].key
                    << "\n";

                clockCache[clockHand].referenceBit = false;

                clockHand =
                    (clockHand + 1) % maxCacheSize;
            }
            else
            {
                T victimKey =
                    clockCache[clockHand].key;

                std::cout
                    << "Evicting key "
                    << victimKey
                    << "\n";

                keyToIndex.erase(victimKey);

                clockCache[clockHand] =
                    {key, true};

                keyToIndex[key] = clockHand;

                std::cout
                    << "Inserted key "
                    << key
                    << "\n";

                clockHand =
                    (clockHand + 1) % maxCacheSize;

                break;
            }
        }
    }

    void printCache()
    {
        std::lock_guard<std::mutex> lock(cacheMutex);

        std::cout << "\n========== CACHE ==========\n";

        for (size_t i = 0; i < clockCache.size(); i++)
        {
            std::cout
                << "Index: "
                << i
                << " | Key: "
                << clockCache[i].key
                << " | RefBit: "
                << clockCache[i].referenceBit;

            if (i == clockHand)
            {
                std::cout << " <- Clock Hand";
            }

            std::cout << "\n";
        }

        std::cout
            << "===========================\n\n";
    }

private:
    size_t maxCacheSize;
    size_t clockHand;

    std::vector<CacheRecord> clockCache;

    std::unordered_map<T, size_t> keyToIndex;

    std::mutex cacheMutex;
};

int main()
{
    ClockSweep<int> cache(3);

    cache.putKey(10);
    cache.putKey(20);
    cache.putKey(30);

    cache.printCache();

    std::cout << "\nAccessing 10 and 20\n\n";

    cache.getKey(10);
    cache.getKey(20);

    cache.printCache();

    std::cout << "\nInserting 40\n\n";

    cache.putKey(40);

    cache.printCache();

    std::cout << "\nInserting 50\n\n";

    cache.putKey(50);

    cache.printCache();

    return 0;
}