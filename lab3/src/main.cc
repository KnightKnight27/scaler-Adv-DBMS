#define LOGGING

#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "ClockSweepCache.h"

static void demoBasic()
{
    std::printf("\n=== Demo 1: basic eviction order ===\n");
    ClockSweepCache<int, std::string> cache(4);

    cache.put(1, "one");
    cache.put(2, "two");
    cache.put(3, "three");
    cache.put(4, "four");

    // Bump usage on keys 2 and 4 so they survive the next sweep.
    (void)cache.get(2);
    (void)cache.get(4);
    (void)cache.get(4);

    // Inserting 5 and 6 should evict keys 1 and 3 first (usage stayed at 1),
    // then start decrementing 2 and 4.
    cache.put(5, "five");
    cache.put(6, "six");

    for (int k : {1, 2, 3, 4, 5, 6}) {
        auto v = cache.get(k);
        std::printf("get(%d) -> %s\n", k, v ? v->c_str() : "MISS");
    }
    std::printf("size=%zu capacity=%zu\n", cache.size(), cache.capacity());
}

static void demoOverwriteAndRemove()
{
    std::printf("\n=== Demo 2: overwrite and remove ===\n");
    ClockSweepCache<std::string, int> cache(3);

    cache.put("a", 1);
    cache.put("b", 2);
    cache.put("c", 3);
    cache.put("b", 22); // overwrite, no eviction

    auto vb = cache.get("b");
    std::printf("get(b) -> %s\n", vb ? std::to_string(*vb).c_str() : "MISS");

    bool removed = cache.remove("a");
    std::printf("remove(a) -> %s\n", removed ? "true" : "false");

    auto va = cache.get("a");
    std::printf("get(a) -> %s\n", va ? std::to_string(*va).c_str() : "MISS");

    cache.put("d", 4); // should fill the freed slot
    std::printf("size=%zu capacity=%zu\n", cache.size(), cache.capacity());
}

static void demoConcurrent()
{
    std::printf("\n=== Demo 3: concurrent put/get ===\n");
    ClockSweepCache<int, int> cache(32);

    auto writer = [&cache]() {
        for (int i = 0; i < 200; ++i) {
            cache.put(i, i * 10);
        }
    };
    auto reader = [&cache]() {
        int hits = 0;
        for (int i = 0; i < 200; ++i) {
            if (cache.get(i)) ++hits;
        }
        std::printf("reader hits=%d\n", hits);
    };

    std::vector<std::thread> threads;
    threads.emplace_back(writer);
    threads.emplace_back(writer);
    threads.emplace_back(reader);
    threads.emplace_back(reader);
    for (auto &t : threads) t.join();

    std::printf("final size=%zu capacity=%zu\n", cache.size(), cache.capacity());
}

int main()
{
    demoBasic();
    demoOverwriteAndRemove();
    demoConcurrent();
    return 0;
}
