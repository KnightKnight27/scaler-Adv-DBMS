// Benchmark: B+ tree index scan vs full sequential scan for point lookups.
//
// We build a table where column `dup` holds the same value as the primary key
// `id`. Looking up by `id` uses the index (IndexScan, O(log n)); looking up by
// `dup` is not indexed, so it forces a full scan (SeqScan + Filter, O(n)). Both
// match exactly one row, so it is a fair comparison.
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "engine.h"

using namespace minidb;
using Clock = std::chrono::steady_clock;

static double timeLookups(Engine& eng, const std::string& column, const std::vector<int>& keys) {
    auto start = Clock::now();
    for (int k : keys) eng.execute("SELECT id FROM bench WHERE " + column + " = " + std::to_string(k));
    auto end = Clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

int main() {
    const int sizes[] = {500, 2000, 8000};
    const int lookups = 300;

    std::cout << "rows  | index total | scan total | index/lookup | scan/lookup | speedup\n";
    std::cout << "------|-------------|------------|--------------|-------------|--------\n";

    for (int n : sizes) {
        std::string dir = "bench_data_" + std::to_string(n);
        std::filesystem::remove_all(dir);
        double indexMs = 0, scanMs = 0;
        {
            Engine eng(dir);  // scoped so its files close before we delete the dir
            eng.execute("CREATE TABLE bench (id INT, dup INT, name TEXT)");
            for (int i = 1; i <= n; ++i)
                eng.execute("INSERT INTO bench VALUES (" + std::to_string(i) + ", " +
                            std::to_string(i) + ", 'u" + std::to_string(i) + "')");

            std::mt19937 rng(12345);
            std::uniform_int_distribution<int> pick(1, n);
            std::vector<int> keys;
            for (int i = 0; i < lookups; ++i) keys.push_back(pick(rng));

            indexMs = timeLookups(eng, "id", keys);
            scanMs = timeLookups(eng, "dup", keys);
            eng.close();
        }
        std::filesystem::remove_all(dir);

        printf("%5d | %9.1fms | %8.1fms | %9.1fus | %8.1fus | %5.1fx\n", n, indexMs, scanMs,
               indexMs * 1000 / lookups, scanMs * 1000 / lookups, scanMs / indexMs);
    }
    return 0;
}
