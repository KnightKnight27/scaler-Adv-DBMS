// Tests for the M2 record codec and the page-backed B+Tree.
#include <algorithm>
#include <cstdio>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <vector>

#include "catalog/record.h"
#include "catalog/schema.h"
#include "common/types.h"
#include "index/bplus_tree.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"

using namespace minidb;

static int g_failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " << #cond << "\n"; \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

static void test_record_codec() {
    Schema schema({{"id", ValueType::INT, 8},
                   {"name", ValueType::VARCHAR, 20},
                   {"score", ValueType::DOUBLE, 8}});
    std::vector<Value> row{std::int64_t{42}, std::string("alice"), double{3.5}};
    std::string bytes = Record::serialize(schema, row);
    std::vector<Value> back = Record::deserialize(schema, bytes);

    CHECK(back.size() == 3);
    CHECK(std::get<std::int64_t>(back[0]) == 42);
    CHECK(std::get<std::string>(back[1]) == "alice");
    CHECK(std::get<double>(back[2]) == 3.5);
    std::cout << "[ok] record codec (round-trip INT/VARCHAR/DOUBLE)\n";
}

static void test_bplus_tree() {
    const std::string path = "test_bt.db";
    std::remove(path.c_str());
    DiskManager dm(path);
    BufferPool pool(32, &dm);

    // Small order forces frequent splits and a multi-level tree with few keys.
    PageId root = BPlusTree::create(&pool);
    BPlusTree tree(&pool, root, /*order=*/4);

    const int N = 1000;
    std::vector<int> keys(N);
    std::iota(keys.begin(), keys.end(), 1);
    std::mt19937 rng(12345);
    std::shuffle(keys.begin(), keys.end(), rng);

    for (int k : keys)
        CHECK(tree.insert(k, RID{static_cast<std::int32_t>(k), static_cast<std::int16_t>(k % 100)}));

    CHECK(tree.height() > 1);   // multi-level => internal splits happened
    CHECK(!tree.insert(500, RID{0, 0}));  // duplicate rejected

    // every key is found, with the RID we stored
    bool all_found = true;
    for (int k = 1; k <= N; ++k) {
        RID r;
        if (!tree.search(k, r) || r.page_id != k) { all_found = false; break; }
    }
    CHECK(all_found);
    RID dummy;
    CHECK(!tree.search(N + 1, dummy));  // absent key

    // range scan [100, 200] returns exactly 101 keys in ascending order
    int count = 0, prev = 0;
    bool ordered = true;
    BTKey rk; RID rr;
    for (auto it = tree.range(100, 200); it.next(rk, rr); ) {
        if (rk <= prev) ordered = false;
        prev = static_cast<int>(rk);
        ++count;
    }
    CHECK(count == 101);
    CHECK(ordered);

    // erase a block of keys; they disappear, the rest remain
    for (int k = 500; k < 600; ++k) CHECK(tree.erase(k));
    RID r;
    CHECK(!tree.search(550, r));
    CHECK(tree.search(499, r) && tree.search(600, r));

    std::remove(path.c_str());
    std::cout << "[ok] b+tree (N=" << N << " height=" << tree.height()
              << " range+erase verified)\n";
}

int main() {
    test_record_codec();
    test_bplus_tree();
    if (g_failures == 0) {
        std::cout << "ALL INDEX TESTS PASSED\n";
        return 0;
    }
    std::cerr << g_failures << " CHECK(s) FAILED\n";
    return 1;
}
