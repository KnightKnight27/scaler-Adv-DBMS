#pragma once

#include <unordered_map>
#include <optional>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include "Data.h"

// frequency cap
static constexpr int MAX_FREQUENCY = 5;

template <class KeyT, class ValueT>
class ClockSweepCache {
public:
    explicit ClockSweepCache(size_t capacity);
    ~ClockSweepCache();

    // returns value if found, bumps its frequency
    std::optional<ValueT> get(const KeyT& key);

    // inserts key; if full, evicts the least frequent entry first
    void put(const KeyT& key, const ValueT& value);

    // manually remove a key
    void remove(const KeyT& key);

private:
    size_t m_Capacity;
    std::unordered_map<KeyT, Data<ValueT>> m_Map;

    std::mutex m_Mutex;
    std::condition_variable m_CV;
    bool m_JobRunning;
    std::unique_ptr<std::thread> m_Job;

    // evicts the entry with the lowest frequency
    void evictLeastFrequent();

    // background thread: decays frequency every second, evicts if freq hits 0
    void sweepLoop();
};

#include "ClockSweepCache.tpp"