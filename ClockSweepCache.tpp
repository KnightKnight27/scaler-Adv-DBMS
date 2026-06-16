#pragma once

#include "ClockSweepCache.h"
#include <cstdio>
#include <algorithm>
#include <limits>
#include <vector>

template <class KeyT, class ValueT>
ClockSweepCache<KeyT, ValueT>::ClockSweepCache(const size_t capacity)
    : m_Capacity(capacity), m_JobRunning(true)
{
    m_Job = std::make_unique<std::thread>(&ClockSweepCache::sweepLoop, this);
}

template <class KeyT, class ValueT>
ClockSweepCache<KeyT, ValueT>::~ClockSweepCache()
{
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_JobRunning = false;
    }
    m_CV.notify_all(); // wake thread immediately so it doesn't wait the full 1s
    m_Job->join();
}

template <class KeyT, class ValueT>
std::optional<ValueT> ClockSweepCache<KeyT, ValueT>::get(const KeyT& key)
{
    std::lock_guard<std::mutex> lock(m_Mutex);

    auto it = m_Map.find(key);
    if (it == m_Map.end())
        return std::nullopt;

    // bump frequency but cap it
    it->second.frequency = std::min(it->second.frequency + 1, MAX_FREQUENCY);
    return it->second.value;
}

template <class KeyT, class ValueT>
void ClockSweepCache<KeyT, ValueT>::put(const KeyT& key, const ValueT& value)
{
    std::lock_guard<std::mutex> lock(m_Mutex);

    // key already exists — just update
    if (m_Map.find(key) != m_Map.end()) {
        m_Map[key].value = value;
        m_Map[key].frequency = std::min(m_Map[key].frequency + 1, MAX_FREQUENCY);
        return;
    }

    // cache full — evict least frequent before inserting
    if (m_Map.size() >= m_Capacity)
        evictLeastFrequent();

    m_Map[key] = {value, 1};
}

template <class KeyT, class ValueT>
void ClockSweepCache<KeyT, ValueT>::remove(const KeyT& key)
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_Map.erase(key);
}

// find the entry with the lowest frequency and remove it
template <class KeyT, class ValueT>
void ClockSweepCache<KeyT, ValueT>::evictLeastFrequent()
{
    // iterator pointing to nothing yet
    auto victim = m_Map.end();
    int minFreq = std::numeric_limits<int>::max();

    for (auto it = m_Map.begin(); it != m_Map.end(); ++it) {
        if (it->second.frequency < minFreq) {
            minFreq = it->second.frequency;
            victim = it;
        }
    }

    if (victim != m_Map.end()) {
#ifdef LOGGING
        printf("Evicting (frequency %d)\n", minFreq);
#endif
        m_Map.erase(victim);
    }
}

// background thread: every second decay all frequencies by 1
// if any entry hits 0, evict it right away
template <class KeyT, class ValueT>
void ClockSweepCache<KeyT, ValueT>::sweepLoop()
{
    using namespace std::chrono_literals;

    while (true) {
        std::unique_lock<std::mutex> lock(m_Mutex);

        // sleep 1s, but exit immediately if destructor signals
        m_CV.wait_for(lock, 1s, [this] { return !m_JobRunning; });

        if (!m_JobRunning) return;

        std::vector<KeyT> toEvict;

        for (auto& [key, data] : m_Map) {
            --data.frequency;
            if (data.frequency <= 0)
                toEvict.push_back(key);
        }

        for (const KeyT& key : toEvict) {
#ifdef LOGGING
            printf("Background evicting key\n");
#endif
            m_Map.erase(key);
        }
    }
}
