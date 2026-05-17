#ifndef CLOCKSWEEPCACHE_H
#define CLOCKSWEEPCACHE_H

#include <cstddef>
#include <cstdio>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

template <class KeyT, class ValueT>
class ClockSweepCache
{
public:
    static constexpr unsigned int kMaxUsageCount = 5;

    explicit ClockSweepCache(std::size_t capacity)
        : m_Capacity(capacity == 0 ? 1 : capacity),
          m_Slots(m_Capacity),
          m_Hand(0)
    {
    }

    std::optional<ValueT> get(const KeyT &key)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);

        auto it = m_Index.find(key);
        if (it == m_Index.end()) {
            return std::nullopt;
        }

        Slot &slot = m_Slots[it->second];
        if (slot.usage < kMaxUsageCount) {
            slot.usage++;
        }
        return slot.value;
    }

    void put(const KeyT &key, const ValueT &value)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);

        auto it = m_Index.find(key);
        if (it != m_Index.end()) {
            Slot &slot = m_Slots[it->second];
            slot.value = value;
            if (slot.usage < kMaxUsageCount) {
                slot.usage++;
            }
            return;
        }

        std::size_t victim = findVictim();
        Slot &slot = m_Slots[victim];

        if (slot.occupied) {
#ifdef LOGGING
            printKeyEviction(slot.key);
#endif
            m_Index.erase(slot.key);
        }

        slot.key = key;
        slot.value = value;
        slot.usage = 1;
        slot.occupied = true;
        m_Index[key] = victim;
    }

    bool remove(const KeyT &key)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);

        auto it = m_Index.find(key);
        if (it == m_Index.end()) {
            return false;
        }

        Slot &slot = m_Slots[it->second];
        slot.occupied = false;
        slot.usage = 0;
        slot.value = ValueT{};
        m_Index.erase(it);
        return true;
    }

    std::size_t size() const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_Index.size();
    }

    std::size_t capacity() const { return m_Capacity; }

private:
    struct Slot {
        KeyT key{};
        ValueT value{};
        unsigned int usage = 0;
        bool occupied = false;
    };

    std::size_t findVictim()
    {
        // Sweep until a slot with usage == 0 is found. Decrement usage on the
        // way and advance the hand. The first pass guarantees a victim because
        // the hand will visit every slot in at most kMaxUsageCount + 1 rounds.
        while (true) {
            Slot &slot = m_Slots[m_Hand];
            if (!slot.occupied) {
                std::size_t victim = m_Hand;
                advanceHand();
                return victim;
            }
            if (slot.usage == 0) {
                std::size_t victim = m_Hand;
                advanceHand();
                return victim;
            }
            slot.usage--;
            advanceHand();
        }
    }

    void advanceHand()
    {
        m_Hand = (m_Hand + 1) % m_Capacity;
    }

    template <class K>
    static void printKeyEviction(const K &k)
    {
        std::printf("Evicting key\n");
        (void)k;
    }

    static void printKeyEviction(int k) { std::printf("Evicting %d\n", k); }
    static void printKeyEviction(long k) { std::printf("Evicting %ld\n", k); }
    static void printKeyEviction(const std::string &k)
    {
        std::printf("Evicting %s\n", k.c_str());
    }

    const std::size_t m_Capacity;
    std::vector<Slot> m_Slots;
    std::unordered_map<KeyT, std::size_t> m_Index;
    std::size_t m_Hand;
    mutable std::mutex m_Mutex;
};

#endif
