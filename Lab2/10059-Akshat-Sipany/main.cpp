#include <iostream>
#include <optional>
#include <vector>
#include <cstdint>
#include <stdexcept>

template <typename T, typename V>
class ClockSweepCache
{
public:
    struct Frame
    {
        T key;
        V value;

        uint32_t usageCount{0};

        bool valid{false};
    };

    ClockSweepCache(uint32_t maxNumber)
        : maxCacheSize(maxNumber),
          frames(maxNumber)
    {
        if (maxCacheSize == 0)
        {
            throw std::invalid_argument("Cache size must be greater than zero");
        }
    }

    void putKey(T key, V value)
    {
        while (true)
        {
            Frame& fr = frames[clockHand];

            if (!fr.valid)
            {
                fr.key = key;
                fr.value = value;

                fr.valid = true;
                fr.usageCount = 1;

                advanceClock();

                return;
            }

            if (fr.usageCount > 0)
            {
                fr.usageCount--;
            }
            else
            {
                fr.key = key;
                fr.value = value;

                fr.usageCount = 1;
                fr.valid = true;

                advanceClock();

                return;
            }

            advanceClock();
        }
    }

    std::optional<V> getKey(T key)
    {
        for (Frame& fr : frames)
        {
            if (fr.valid && fr.key == key)
            {
                fr.usageCount++;

                return fr.value;
            }
        }

        return std::nullopt;
    }

    void printState()
    {
        std::cout << "\nCache State:\n";

        for (uint32_t i = 0; i < maxCacheSize; i++)
        {
            Frame& fr = frames[i];

            std::cout << "[" << i << "] ";

            if (fr.valid)
            {
                std::cout
                    << "Key=" << fr.key
                    << " Value=" << fr.value
                    << " Usage=" << fr.usageCount;
            }
            else
            {
                std::cout << "EMPTY";
            }

            if (i == clockHand)
            {
                std::cout << " <-- clock";
            }

            std::cout << "\n";
        }
    }

private:
    void advanceClock()
    {
        clockHand =
            (clockHand + 1) % maxCacheSize;
    }

private:
    uint32_t maxCacheSize{0};

    uint32_t clockHand{0};

    std::vector<Frame> frames;
};

int main()
{
    ClockSweepCache<int, int> cache(3);

    cache.putKey(1, 100);
    cache.putKey(2, 200);
    cache.putKey(3, 300);

    cache.printState();

    auto result = cache.getKey(1);

    if (result)
    {
        std::cout
            << "\nFound value: "
            << *result
            << "\n";
    }

    cache.putKey(4, 400);

    cache.printState();

    return 0;
}
