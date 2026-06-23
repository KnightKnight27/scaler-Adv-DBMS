#include "minidb/storage/index.hpp"
#include "test_util.hpp"
using namespace minidb;

static void run_tests() {
  BPlusTree t;
  // Enough inserts to force many splits (order is 64).
  for (int i = 0; i < 2000; ++i) t.insert(Value((int64_t)i), RID{(uint32_t)i, 0});
  CHECK_EQ(t.size(), (size_t)2000);
  CHECK_EQ(t.find(Value((int64_t)1234)).size(), (size_t)1);
  CHECK_EQ(t.find(Value((int64_t)1234))[0].page_id, (uint32_t)1234);
  CHECK(t.find(Value((int64_t)999999)).empty());

  // Duplicate keys map to multiple RIDs.
  t.insert(Value((int64_t)5), RID{777, 1});
  CHECK_EQ(t.find(Value((int64_t)5)).size(), (size_t)2);

  // Inclusive range [10, 13] => keys 10,11,12,13.
  auto r = t.range(Value((int64_t)10), true, Value((int64_t)13), true);
  CHECK_EQ(r.size(), (size_t)4);
  // Exclusive upper bound [10, 13) => 10,11,12.
  auto r2 = t.range(Value((int64_t)10), true, Value((int64_t)13), false);
  CHECK_EQ(r2.size(), (size_t)3);

  // Remove a specific (key, rid); the other RID for key 5 survives.
  CHECK(t.remove(Value((int64_t)5), RID{777, 1}));
  CHECK_EQ(t.find(Value((int64_t)5)).size(), (size_t)1);
  // Removing the last RID for a key makes it disappear.
  CHECK(t.remove(Value((int64_t)5), RID{5, 0}));
  CHECK(t.find(Value((int64_t)5)).empty());
  // Removing an absent (key,rid) returns false.
  CHECK(!t.remove(Value((int64_t)42), RID{1, 1}));

  // VARCHAR keys order correctly.
  BPlusTree s;
  s.insert(Value(std::string("banana")), RID{2, 0});
  s.insert(Value(std::string("apple")), RID{1, 0});
  s.insert(Value(std::string("cherry")), RID{3, 0});
  auto sr = s.range(Value(std::string("apple")), true, Value(std::string("banana")), true);
  CHECK_EQ(sr.size(), (size_t)2);
}

TEST_MAIN()
