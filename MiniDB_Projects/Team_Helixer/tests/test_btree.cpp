// Stress-tests the page-based B+Tree: thousands of inserts (forcing leaf AND
// internal node splits => a multi-level tree), point lookups, a range scan, and
// deletions. Runs through the buffer pool so the index shares the cache.
#include <cassert>
#include <cstdio>
#include <algorithm>
#include <numeric>
#include <random>
#include <iostream>
#include "storage/disk_manager.h"
#include "storage/buffer_pool.h"
#include "index/btree.h"

using namespace minidb;

int main() {
    const char *db = "test_btree.db";
    std::remove(db);
    {
        DiskManager disk(db);
        BufferPoolManager bpm(128, &disk);
        BPlusTree tree(&bpm);

        const int N = 50000;
        std::vector<int> keys(N);
        std::iota(keys.begin(), keys.end(), 0);
        std::mt19937 rng(42);
        std::shuffle(keys.begin(), keys.end(), rng);

        // Insert all keys in shuffled order. rid.slot encodes the key for checking.
        for (int k : keys) {
            assert(tree.insert(k, RID{ k % 97, k }));
        }
        // Duplicate insert must be rejected.
        assert(!tree.insert(keys[0], RID{0, 0}));
        assert(!tree.empty());

        // Every key must be found with the exact rid we stored.
        for (int k = 0; k < N; ++k) {
            RID r;
            assert(tree.search(k, &r));
            assert(r.slot_id == k && r.page_id == k % 97);
        }
        // A key we never inserted must not be found.
        RID dummy;
        assert(!tree.search(N + 5, &dummy));

        // Range scan [1000, 1099] must return exactly 100 keys in sorted order.
        auto rs = tree.range(1000, 1099);
        assert(rs.size() == 100);
        for (size_t i = 0; i < rs.size(); ++i) assert(rs[i].slot_id == 1000 + (int)i);

        // Delete the even keys; verify they're gone and odd keys remain.
        for (int k = 0; k < N; k += 2) assert(tree.remove(k));
        for (int k = 0; k < N; ++k) {
            RID r;
            bool found = tree.search(k, &r);
            assert(found == (k % 2 == 1));
        }
        std::cout << "[OK] B+Tree: " << N << " inserts w/ splits, point lookups, "
                     "range scan, deletes all verified (multi-level tree)" << std::endl;
    }
    std::remove(db);
    return 0;
}
