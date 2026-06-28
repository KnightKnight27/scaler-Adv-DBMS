#include <iostream>
#include <vector>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <stdexcept>

template <typename T>
class ClockSweep
{
public:
  // Constructor initializes the cache size and starts the background aging thread
  ClockSweep(int maxNumber) : maxCacheSize(maxNumber), hand(0), running(true)
  {
    if (maxNumber <= 0)
    {
      throw std::invalid_argument("Cache size must be greater than 0");
    }
    buffer.reserve(maxCacheSize);
    bgClockThread = std::thread(&ClockSweep::backgroundSweep, this);
  };

  // Destructor ensures the background thread is cleanly joined
  ~ClockSweep()
  {
    running = false;
    if (bgClockThread.joinable())
    {
      bgClockThread.join();
    }
  }

  // Retrieves the key. Sets the reference bit to 1 (true) to give it a "second chance"
  T getKey(T key)
  {
    std::lock_guard<std::mutex> lock(mtx);

    auto it = cacheMap.find(key);
    if (it != cacheMap.end())
    {
      // Key found, update its reference bit
      buffer[it->second].second = true;
      return buffer[it->second].first;
    }

    // Throw if not found. Alternatively, you could return a default T{}
    throw std::out_of_range("Key not found in cache");
  }

  // Inserts a key into the cache, using the clock algorithm for eviction if full
  void putKey(T key)
  {
    std::lock_guard<std::mutex> lock(mtx);

    // Case 1: Key already exists. Update its reference bit and return.
    auto it = cacheMap.find(key);
    if (it != cacheMap.end())
    {
      buffer[it->second].second = true;
      return;
    }

    // Case 2: Cache is not full yet. Append to the buffer.
    if (buffer.size() < maxCacheSize)
    {
      buffer.push_back({key, true});
      cacheMap[key] = buffer.size() - 1;
      return;
    }

    // Case 3: Cache is full. Sweep the clock hand to find a victim.
    while (true)
    {
      if (!buffer[hand].second)
      {
        // Found an entry with a reference bit of 0. Evict it.
        T oldKey = buffer[hand].first;
        cacheMap.erase(oldKey);

        // Replace with the new key
        buffer[hand] = {key, true};
        cacheMap[key] = hand;

        // Advance the hand
        hand = (hand + 1) % maxCacheSize;
        break;
      }
      else
      {
        // Entry has a reference bit of 1.
        // Give it a second chance (set to 0) and advance the hand.
        buffer[hand].second = false;
        hand = (hand + 1) % maxCacheSize;
      }
    }
  }

  // Helper method for demonstration purposes to view cache state
  void printCache()
  {
    std::lock_guard<std::mutex> lock(mtx);
    std::cout << "Cache State (Hand at index " << hand << "):\n";
    for (size_t i = 0; i < buffer.size(); ++i)
    {
      std::cout << "[" << i << "] Key: " << buffer[i].first
                << " | RefBit: " << buffer[i].second << "\n";
    }
    std::cout << "-------------------\n";
  }

private:
  uint maxCacheSize{0u};
  size_t hand{0};

  // Circular buffer holds pairs of <Key, ReferenceBit>
  std::vector<std::pair<T, bool>> buffer;

  // Hash map holds Key -> Index in the buffer for O(1) lookups
  std::unordered_map<T, size_t> cacheMap;

  // Threading primitives
  std::thread bgClockThread;
  std::mutex mtx;
  std::atomic<bool> running{false};

  // Background worker that periodically decays the reference bits
  void backgroundSweep()
  {
    while (running)
    {
      // Sleep for a duration (e.g., 5 seconds) before sweeping again
      std::this_thread::sleep_for(std::chrono::seconds(5));

      std::lock_guard<std::mutex> lock(mtx);
      // Decay all reference bits to false (simulating aging)
      for (auto &entry : buffer)
      {
        entry.second = false;
      }
    }
  }
};

int main()
{
  // Instantiate with a max size (e.g., 3)
  ClockSweep<int> clockSweep(3);

  std::cout << "Putting 1, 2, 3...\n";
  clockSweep.putKey(1);
  clockSweep.putKey(2);
  clockSweep.putKey(3);
  clockSweep.printCache();

  std::cout << "Accessing 1 to set its reference bit to 1...\n";
  clockSweep.getKey(1);
  clockSweep.printCache();

  std::cout << "Putting 4. This should trigger an eviction.\n";
  // 1 has a ref bit of 1, so it gets a second chance (ref bit becomes 0).
  // 2 has a ref bit of 1 (set on insertion), so it gets a second chance.
  // Wait, all were set to 1 on insertion.
  // Hand sweeps: 1->0, 2->0, 3->0, 1(victim). 4 replaces 1.
  clockSweep.putKey(4);
  clockSweep.printCache();

  return 0;
}