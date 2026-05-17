#ifndef CLOCKSWEEP_H
#define CLOCKSWEEP_H

#include <iostream>
#include <optional>
#include <unordered_map>
#include <vector>

typedef unsigned int uint;

template<typename T>
class ClockSweep
{
public:

    explicit ClockSweep(int maxNumber): maxCacheSize(maxNumber),clockHand(0)
    {

    }

    std::optional<T> getKey(T key)
    {
        auto it = pageIndex.find(key);
        if (it == pageIndex.end())
        {
            return std::nullopt;
        }
        pages[it->second].refBit = true;
        return pages[it->second].value;
    }

    void putKey(T key)
    {
        auto it = pageIndex.find(key);
        if (it != pageIndex.end())
        {
            pages[it->second].refBit = true;
            std::cout
                << "Key "
                << key
                << " already exists\n";
            return;
        }

        if (pages.size() < maxCacheSize)
        {
            pages.push_back({key, true});
            pageIndex[key] = pages.size() - 1;
            std::cout
                << "Inserted key "
                << key
                << "\n";

            return;
        }

        while (true)
        {
            if (!pages[clockHand].refBit)
            {
                T oldKey = pages[clockHand].value;
                std::cout
                    << "Evicting key "
                    << oldKey
                    << "\n";

                pageIndex.erase(oldKey);
                pages[clockHand] = {key, true};
                pageIndex[key] = clockHand;
                std::cout
                    << "Inserted key "
                    << key
                    << " at slot "
                    << clockHand
                    << "\n";
                clockHand =
                    (clockHand + 1) % maxCacheSize;

                break;
            }
            pages[clockHand].refBit = false;
            clockHand =
                (clockHand + 1) % maxCacheSize;
        }
    }

    void display() const
    {
        std::cout << "\nCache State:\n";
        for (const auto& page : pages)
        {
            std::cout
                << "["
                << page.value
                << " ref="
                << page.refBit
                << "] ";
        }
        std::cout
            << "\nClock Hand -> "
            << clockHand
            << "\n\n";
    }

private:
    struct Page
    {
        T value;
        bool refBit;
    };

    uint maxCacheSize{0u};
    uint clockHand{0u};
    std::vector<Page> pages;
    std::unordered_map<T, uint> pageIndex;
};

#endif