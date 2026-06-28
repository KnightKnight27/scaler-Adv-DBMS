// M2 test — B+ tree correctness against a std::map reference oracle.
// Covers: insert+split, point search, range scan order, delete with
// borrow/merge, and the invariant that the tree always agrees with the oracle.
#include "bplus_tree.hpp"
#include <cassert>
#include <cstdio>
#include <map>
#include <vector>

using namespace minidb;

static RID rid_of(int64_t k) { return RID{(int32_t)(k / 100), (uint16_t)(k % 100)}; }

int main() {
    BPlusTree tree(4);            // small order -> lots of splits/merges
    std::map<int64_t, RID> oracle;

    // deterministic but non-sorted insertion order
    std::vector<int64_t> keys;
    for (int i = 0; i < 500; ++i) keys.push_back((i * 137 + 17) % 1000);

    for (int64_t k : keys) {
        bool a = tree.insert(k, rid_of(k));
        bool b = oracle.emplace(k, rid_of(k)).second;
        assert(a == b && "insert acceptance must match oracle (dup handling)");
    }
    std::printf("[M2] inserted %zu unique keys, tree height = %d\n",
                oracle.size(), tree.height());

    // every key present, every absent key absent
    for (int64_t k = 0; k < 1000; ++k) {
        RID r;
        bool found = tree.search(k, &r);
        bool exp   = oracle.count(k) > 0;
        assert(found == exp);
        if (found) assert(r == oracle[k]);
    }
    std::printf("[M2] point search matches oracle for all 1000 candidates\n");

    // range scan returns keys in sorted order, matching the oracle slice
    {
        auto rows = tree.range(200, 700);
        std::vector<int64_t> got, want;
        for (auto& [k, v] : rows) got.push_back(k);
        for (auto& [k, v] : oracle) if (k >= 200 && k <= 700) want.push_back(k);
        assert(got == want && "range scan must be sorted and complete");
        std::printf("[M2] range [200,700] returned %zu keys, sorted + correct\n",
                    got.size());
    }

    // delete half the keys, re-verify the whole space after each batch
    int erased = 0;
    for (int64_t k : keys) {
        if (k % 2 == 0) {
            bool a = tree.erase(k);
            bool b = oracle.erase(k) > 0;
            assert(a == b);
            if (a) ++erased;
        }
    }
    for (int64_t k = 0; k < 1000; ++k) {
        RID r;
        assert(tree.search(k, &r) == (oracle.count(k) > 0));
    }
    std::printf("[M2] erased %d keys, tree still consistent with oracle\n", erased);

    std::printf("[M2] B+ tree: ALL CHECKS PASSED\n");
    return 0;
}
