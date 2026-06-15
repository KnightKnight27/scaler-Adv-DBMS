#ifndef CLOCKSWEEP_H
#define CLOCKSWEEP_H

#include <iostream>
#include <optional>
#include <unordered_map>
#include <vector>
#include <stdexcept>

template <typename T>
class ClockSweep
{
public:
    explicit ClockSweep(int capacity)
        : maxCacheSize(capacity), hand(0)
    {
        if (capacity <= 0)
        {
            throw std::invalid_argument("Cache size must be greater than zero");
        }
    }

    std::optional<T> getKey(const T& key)
    {
        auto it = positions.find(key);
        if (it == positions.end())
        {
            return std::nullopt;
        }

        pages[it->second].referenced = true;
        return pages[it->second].value;
    }

    void putKey(const T& key)
    {
        auto found = positions.find(key);
        if (found != positions.end())
        {
            pages[found->second].referenced = true;
            std::cout << "Key " << key << " already exists\n";
            return;
        }

        if (pages.size() < static_cast<std::size_t>(maxCacheSize))
        {
            pages.push_back({key, true});
            positions[key] = pages.size() - 1;

            std::cout << "Inserted key " << key << "\n";
            return;
        }

        while (true)
        {
            if (!pages[hand].referenced)
            {
                T removedKey = pages[hand].value;
                std::cout << "Evicting key " << removedKey << "\n";

                positions.erase(removedKey);
                pages[hand] = {key, true};
                positions[key] = hand;

                std::cout << "Inserted key " << key << " at slot " << hand << "\n";

                hand = (hand + 1) % maxCacheSize;
                break;
            }

            pages[hand].referenced = false;
            hand = (hand + 1) % maxCacheSize;
        }
    }

    void display() const
    {
        std::cout << "\nCache State:\n";
        for (const auto& page : pages)
        {
            std::cout << "[" << page.value << " ref=" << page.referenced << "] ";
        }
        std::cout << "\nClock Hand -> " << hand << "\n\n";
    }

private:
    struct Page
    {
        T value{};
        bool referenced{false};
    };

    int maxCacheSize;
    int hand;
    std::vector<Page> pages;
    std::unordered_map<T, std::size_t> positions;
};

#endif