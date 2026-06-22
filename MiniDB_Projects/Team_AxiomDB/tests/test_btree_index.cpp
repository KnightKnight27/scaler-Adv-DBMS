#include "catch.hpp"

#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <numeric>
#include <random>
#include <string>
#include <vector>

#include "buffer/buffer_pool_manager.h"
#include "common/serialize.h"
#include "index/bplus_tree_index.h"
#include "storage/disk_storage_manager.h"

using namespace axiomdb;

namespace {
std::string temp_db_path(const char* tag) {
  return std::string("/tmp/axiomdb_test_") + tag + "_" + std::to_string(::getpid()) + ".wdb";
}
RID rid_for(int i) { return RID{i, static_cast<slot_id_t>(i % 60)}; }
}  // namespace

TEST_CASE("B+tree insert forces multi-level splits; search finds all", "[btree]") {
  std::string path = temp_db_path("btree_big");
  ::remove(path.c_str());
  DiskStorageManager dm(path);
  BufferPoolManager bp(&dm, /*pool_size=*/64, /*k=*/2);  // << number of pages -> real paging

  BPlusTreeIndex tree = BPlusTreeIndex::create(&bp);

  const int N = 5000;
  for (int i = 0; i < N; ++i) {
    REQUIRE(tree.insert(encode_int64_key(i), rid_for(i)));
  }
  // 5000 keys at fanout ~56 must produce at least 2 internal levels.
  REQUIRE(tree.height() >= 3);

  for (int i = 0; i < N; ++i) {
    auto r = tree.search(encode_int64_key(i));
    REQUIRE(r.has_value());
    REQUIRE(*r == rid_for(i));
  }
  REQUIRE_FALSE(tree.search(encode_int64_key(N + 1)).has_value());
}

TEST_CASE("B+tree handles random insertion order and is fully ordered on scan", "[btree]") {
  std::string path = temp_db_path("btree_rand");
  ::remove(path.c_str());
  DiskStorageManager dm(path);
  BufferPoolManager bp(&dm, 64, 2);
  BPlusTreeIndex tree = BPlusTreeIndex::create(&bp);

  const int N = 3000;
  std::vector<int> keys(N);
  std::iota(keys.begin(), keys.end(), 0);
  std::mt19937 rng(12345);
  std::shuffle(keys.begin(), keys.end(), rng);
  for (int k : keys) tree.insert(encode_int64_key(k), rid_for(k));

  // Full scan must visit all keys in ascending numeric order.
  int expected = 0;
  int count = 0;
  for (auto it = tree.begin(); it.valid(); it.next()) {
    REQUIRE(decode_int64_key(it.key()) == expected);
    ++expected;
    ++count;
  }
  REQUIRE(count == N);
}

TEST_CASE("B+tree insert is an upsert", "[btree]") {
  std::string path = temp_db_path("btree_upsert");
  ::remove(path.c_str());
  DiskStorageManager dm(path);
  BufferPoolManager bp(&dm, 16, 2);
  BPlusTreeIndex tree = BPlusTreeIndex::create(&bp);

  REQUIRE(tree.insert(encode_int64_key(7), RID{1, 1}));        // new
  REQUIRE_FALSE(tree.insert(encode_int64_key(7), RID{9, 9}));  // overwrite
  REQUIRE(tree.search(encode_int64_key(7)).value() == (RID{9, 9}));
}

TEST_CASE("B+tree lazy delete removes keys; survivors remain searchable", "[btree]") {
  std::string path = temp_db_path("btree_del");
  ::remove(path.c_str());
  DiskStorageManager dm(path);
  BufferPoolManager bp(&dm, 64, 2);
  BPlusTreeIndex tree = BPlusTreeIndex::create(&bp);

  const int N = 2000;
  for (int i = 0; i < N; ++i) tree.insert(encode_int64_key(i), rid_for(i));

  for (int i = 0; i < N; i += 2) REQUIRE(tree.erase(encode_int64_key(i)));   // delete evens
  REQUIRE_FALSE(tree.erase(encode_int64_key(0)));                           // already gone

  int count = 0;
  for (auto it = tree.begin(); it.valid(); it.next()) {
    REQUIRE(decode_int64_key(it.key()) % 2 == 1);  // only odds survive
    ++count;
  }
  REQUIRE(count == N / 2);
  REQUIRE(tree.search(encode_int64_key(1)).has_value());
  REQUIRE_FALSE(tree.search(encode_int64_key(2)).has_value());
}

TEST_CASE("B+tree bounded range scan [lo,hi)", "[btree]") {
  std::string path = temp_db_path("btree_range");
  ::remove(path.c_str());
  DiskStorageManager dm(path);
  BufferPoolManager bp(&dm, 32, 2);
  BPlusTreeIndex tree = BPlusTreeIndex::create(&bp);
  for (int i = 0; i < 1000; ++i) tree.insert(encode_int64_key(i), rid_for(i));

  std::vector<int> got;
  for (auto it = tree.range(encode_int64_key(100), encode_int64_key(110)); it.valid(); it.next()) {
    got.push_back(static_cast<int>(decode_int64_key(it.key())));
  }
  REQUIRE(got.size() == 10);
  REQUIRE(got.front() == 100);
  REQUIRE(got.back() == 109);  // hi is exclusive
}

TEST_CASE("B+tree persists across reopen via meta page", "[btree]") {
  std::string path = temp_db_path("btree_persist");
  ::remove(path.c_str());

  page_id_t meta;
  const int N = 4000;
  {
    DiskStorageManager dm(path);
    BufferPoolManager bp(&dm, 64, 2);
    BPlusTreeIndex tree = BPlusTreeIndex::create(&bp);
    meta = tree.meta_page_id();
    for (int i = 0; i < N; ++i) tree.insert(encode_int64_key(i), rid_for(i));
    bp.flush_all();
    dm.sync();
  }
  {
    DiskStorageManager dm(path);
    BufferPoolManager bp(&dm, 64, 2);
    BPlusTreeIndex tree(&bp, meta);  // reopen from persisted meta page
    REQUIRE(tree.height() >= 3);
    for (int i = 0; i < N; ++i) REQUIRE(tree.search(encode_int64_key(i)).value() == rid_for(i));
    // Still mutable after reopen.
    REQUIRE(tree.insert(encode_int64_key(N), rid_for(N)));
    REQUIRE(tree.search(encode_int64_key(N)).has_value());
  }
  ::remove(path.c_str());
}

TEST_CASE("B+tree rejects oversized keys", "[btree]") {
  std::string path = temp_db_path("btree_bigkey");
  ::remove(path.c_str());
  DiskStorageManager dm(path);
  BufferPoolManager bp(&dm, 16, 2);
  BPlusTreeIndex tree = BPlusTreeIndex::create(&bp);
  std::string huge(BPlusTreeIndex::MAX_KEY_LEN + 1, 'x');
  REQUIRE_THROWS(tree.insert(huge, RID{0, 0}));
}
