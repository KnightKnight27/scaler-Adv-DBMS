// B+Tree index: point search, duplicates, range scan, and removal.
#include "index/bplus_tree.h"
#include "test_util.h"

using namespace minidb;

int main() {
    BPlusTree t;
    const int N = 1000;
    for (int i = 0; i < N; ++i) {
        int k = (i * 37 + 11) % N;  // scrambled insertion order
        t.Insert(Value::MakeInt(k), RID(k, 0));
    }

    int found = 0;
    for (int k = 0; k < N; ++k) {
        auto r = t.Search(Value::MakeInt(k));
        if (r.size() == 1 && r[0].page_id == k) ++found;
    }
    CHECK(found == N);

    // Duplicate key keeps multiple RIDs (secondary-index behavior).
    t.Insert(Value::MakeInt(500), RID(500, 1));
    CHECK(t.Search(Value::MakeInt(500)).size() == 2);

    // Range scan is inclusive on both ends.
    CHECK(t.RangeScan(Value::MakeInt(100), Value::MakeInt(109)).size() == 10);

    // Remove both RIDs of key 500 -> gone.
    t.Remove(Value::MakeInt(500), RID(500, 0));
    t.Remove(Value::MakeInt(500), RID(500, 1));
    CHECK(t.Search(Value::MakeInt(500)).empty());

    CHECK(t.Height() >= 1);
    return minidb_test::Done("index");
}
