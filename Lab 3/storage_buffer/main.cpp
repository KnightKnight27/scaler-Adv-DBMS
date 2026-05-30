#include <iostream>
#include <unordered_map>
#include <vector>
#include <optional>

using namespace std;

template <typename T>
class ClockSweep
{
private:
  struct Entry
  {
    T key;
    bool referenceBit;

    Entry() : referenceBit(false) {}
  };

  unsigned int maxCacheSize;
  unsigned int currentSize;
  unsigned int clockHand;

  vector<Entry> cache;
  unordered_map<T, size_t> keyToIndex;

public:
  ClockSweep(unsigned int capacity)
  {
    if (capacity == 0)
    {
      throw invalid_argument("Cache size must be greater than 0");
    }

    maxCacheSize = capacity;
    currentSize = 0;
    clockHand = 0;

    cache.resize(capacity);
  }

  optional<T> getKey(const T &key)
  {
    auto it = keyToIndex.find(key);

    if (it == keyToIndex.end())
    {
      return nullopt;
    }

    cache[it->second].referenceBit = true;

    return cache[it->second].key;
  }

  void putKey(const T &key)
  {
    auto it = keyToIndex.find(key);

    // Key already exists
    if (it != keyToIndex.end())
    {
      cache[it->second].referenceBit = true;
      return;
    }

    // Free slot available
    if (currentSize < maxCacheSize)
    {
      cache[currentSize].key = key;
      cache[currentSize].referenceBit = true;

      keyToIndex[key] = currentSize;

      currentSize++;
      return;
    }

    // Clock Sweep Replacement
    while (true)
    {
      Entry &current = cache[clockHand];

      if (current.referenceBit == false)
      {
        keyToIndex.erase(current.key);

        current.key = key;
        current.referenceBit = true;

        keyToIndex[key] = clockHand;

        clockHand = (clockHand + 1) % maxCacheSize;

        return;
      }

      current.referenceBit = false;
      clockHand = (clockHand + 1) % maxCacheSize;
    }
  }

  bool contains(const T &key) const
  {
    return keyToIndex.find(key) != keyToIndex.end();
  }

  void printCache() const
  {
    cout << "Cache State\n";

    for (unsigned int i = 0; i < currentSize; i++)
    {
      cout << "[" << i << "] "
           << "Key = " << cache[i].key
           << ", RefBit = "
           << (cache[i].referenceBit ? 1 : 0)
           << endl;
    }

    cout << "Clock Hand = " << clockHand << endl;
    cout << endl;
  }
};

int main()
{
  ClockSweep<int> cache(3);

  cache.putKey(1);
  cache.putKey(2);
  cache.putKey(3);

  cout << "Initial Cache:\n";
  cache.printCache();

  cache.getKey(1);
  cache.getKey(2);

  cache.putKey(4);

  cout << "After inserting 4:\n";
  cache.printCache();

  cout << "Contains 1: " << cache.contains(1) << endl;
  cout << "Contains 2: " << cache.contains(2) << endl;
  cout << "Contains 3: " << cache.contains(3) << endl;
  cout << "Contains 4: " << cache.contains(4) << endl;

  return 0;
}