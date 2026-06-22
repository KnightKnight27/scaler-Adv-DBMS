// Tests for the B+ tree index: search, splits, ordered walk, range, delete.
#include "btree.h"
#include "check.h"

using namespace minidb;

static RID rid(int k) { return RID{k, 0}; }

int main() {
    BPlusTree t(4);
    int keys[] = {50, 20, 80, 10, 30, 60, 90, 5, 25, 35, 70, 100, 1, 2, 3};
    for (int k : keys) t.insert(k, rid(k));

    for (int k : keys) {
        RID r;
        CHECK(t.search(k, r) && r.page == k);
    }
    RID r;
    CHECK(!t.search(999, r));      // missing key
    CHECK(t.height() > 1);          // splits grew the tree past one leaf

    // ordered walk via leaf chain
    auto items = t.items();
    bool sorted = true;
    for (size_t i = 1; i < items.size(); ++i)
        if (items[i].first < items[i - 1].first) sorted = false;
    CHECK(sorted);
    CHECK(items.size() == 15);

    // range scan
    auto rg = t.rangeScan(20, 60);
    CHECK(rg.size() == 6);          // 20 25 30 35 50 60

    // update existing key replaces its RID
    t.insert(50, RID{777, 7});
    CHECK(t.search(50, r) && r.page == 777);

    // delete (lazy)
    CHECK(t.erase(30));
    CHECK(!t.search(30, r));
    CHECK(!t.erase(30));            // already gone
    CHECK(t.search(35, r));         // neighbour untouched

    REPORT();
}
