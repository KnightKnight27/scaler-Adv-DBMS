#include "storage.h"
#include "buffer_pool.h"
#include "btree.h"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <vector>

static const std::string DB_FILE = "/tmp/test_btree_engine.db";
static void cleanup() { std::filesystem::remove(DB_FILE); }

// ─── test 1: 10 000 sequential keys ─────────────────────────────────────────

static void test_sequential_10k() {
    cleanup();
    DiskManager dm(DB_FILE);
    BufferPoolManager bpm(512, &dm);
    BPlusTree tree(&bpm);

    const int N = 10000;
    for (int i = 0; i < N; ++i)
        tree.Insert(static_cast<int64_t>(i), static_cast<int64_t>(i) * 2);

    for (int i = 0; i < N; ++i) {
        auto v = tree.Search(static_cast<int64_t>(i));
        assert(v.has_value() && v.value() == static_cast<int64_t>(i) * 2);
    }

    // No keys outside the inserted range should be found.
    assert(!tree.Search(-1).has_value());
    assert(!tree.Search(N).has_value());

    assert(bpm.AllUnpinned());
    std::cout << "[PASS] test_sequential_10k\n";
}

// ─── test 2: 5 000 random keys ───────────────────────────────────────────────

static void test_random_5k() {
    cleanup();
    DiskManager dm(DB_FILE);
    BufferPoolManager bpm(512, &dm);
    BPlusTree tree(&bpm);

    const int N = 5000;
    std::vector<int64_t> keys(N);
    for (int i = 0; i < N; ++i) keys[i] = static_cast<int64_t>(i);

    // Deterministic Fisher-Yates shuffle.
    std::srand(42);
    for (int i = N - 1; i > 0; --i) {
        int j = static_cast<int>(static_cast<unsigned>(std::rand()) % static_cast<unsigned>(i + 1));
        std::swap(keys[i], keys[j]);
    }

    for (int i = 0; i < N; ++i)
        tree.Insert(keys[i], keys[i] * 3);

    // Search every key in insertion order.
    for (int i = 0; i < N; ++i) {
        auto v = tree.Search(keys[i]);
        assert(v.has_value() && v.value() == keys[i] * 3);
    }

    // Also search in sorted order.
    for (int64_t k = 0; k < N; ++k) {
        auto v = tree.Search(k);
        assert(v.has_value() && v.value() == k * 3);
    }

    assert(bpm.AllUnpinned());
    std::cout << "[PASS] test_random_5k\n";
}

// ─── test 3: zero pin leaks under mixed workload ─────────────────────────────

static void test_no_pin_leaks() {
    cleanup();
    DiskManager dm(DB_FILE);
    BufferPoolManager bpm(512, &dm);
    BPlusTree tree(&bpm);

    // Insert 1 000 keys.
    for (int i = 0; i < 1000; ++i)
        tree.Insert(static_cast<int64_t>(i), static_cast<int64_t>(i));
    assert(bpm.AllUnpinned());

    // Search all of them.
    for (int i = 0; i < 1000; ++i) {
        auto v = tree.Search(static_cast<int64_t>(i));
        assert(v.has_value() && v.value() == static_cast<int64_t>(i));
    }
    assert(bpm.AllUnpinned());

    // Overwrite 500 keys and add 500 new ones (overlap region tests upsert).
    for (int i = 500; i < 1500; ++i)
        tree.Insert(static_cast<int64_t>(i), static_cast<int64_t>(i) * 2);
    assert(bpm.AllUnpinned());

    // Verify: original range [0,500) unchanged.
    for (int i = 0; i < 500; ++i) {
        auto v = tree.Search(static_cast<int64_t>(i));
        assert(v.has_value() && v.value() == static_cast<int64_t>(i));
    }
    // Verify: overwritten range [500,1000) and new range [1000,1500).
    for (int i = 500; i < 1500; ++i) {
        auto v = tree.Search(static_cast<int64_t>(i));
        assert(v.has_value() && v.value() == static_cast<int64_t>(i) * 2);
    }
    assert(bpm.AllUnpinned());

    // Delete a few keys and verify.
    for (int i = 0; i < 100; ++i)
        tree.Delete(static_cast<int64_t>(i));
    assert(bpm.AllUnpinned());

    for (int i = 0; i < 100; ++i)
        assert(!tree.Search(static_cast<int64_t>(i)).has_value());
    for (int i = 100; i < 500; ++i) {
        auto v = tree.Search(static_cast<int64_t>(i));
        assert(v.has_value() && v.value() == static_cast<int64_t>(i));
    }
    assert(bpm.AllUnpinned());

    std::cout << "[PASS] test_no_pin_leaks\n";
}

// ─── main ────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "=== B+ Tree Tests ===\n";
    test_sequential_10k();
    test_random_5k();
    test_no_pin_leaks();
    cleanup();
    std::cout << "\nAll B+ Tree tests passed.\n";
    return 0;
}
