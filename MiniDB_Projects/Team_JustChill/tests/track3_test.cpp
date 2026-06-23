// Standalone Track 3 verification driver.
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>

#include "btree.h"
#include "execution.h"
#include "lock_manager.h"

using namespace minidb;

static int g_checks = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    ++g_checks;                                                         \
    if (!(cond)) {                                                      \
      std::cerr << "FAIL: " << #cond << " @ line " << __LINE__ << "\n"; \
      std::exit(1);                                                     \
    }                                                                   \
  } while (0)

static void testBTree() {
  BPlusTree t;
  // Insert enough keys to force several splits (kOrder = 64).
  for (int k = 0; k < 1000; ++k) t.insert(k, RID{static_cast<uint32_t>(k), 0});

  // Point search.
  for (int k = 0; k < 1000; ++k) {
    auto r = t.search(k);
    CHECK(r.has_value());
    CHECK(r->page_id == static_cast<uint32_t>(k));
  }
  CHECK(!t.search(5000).has_value());

  // Range scan [100, 199] should yield 100 ascending live keys.
  int count = 0, expect = 100;
  for (auto it = t.range(100, 199); it.valid(); it.next()) {
    CHECK(it.key() == expect);
    ++expect;
    ++count;
  }
  CHECK(count == 100);

  // Tombstone delete: removed keys vanish from search and scans, no rebalance.
  CHECK(t.remove(150));
  CHECK(!t.remove(150));            // already gone
  CHECK(!t.search(150).has_value());
  count = 0;
  for (auto it = t.range(149, 151); it.valid(); it.next()) ++count;
  CHECK(count == 2);  // 149 and 151, but not 150
  std::cout << "[ok] BTree: insert/search/range/tombstone-delete\n";
}

static void testLockManager() {
  LockManager lm;
  // Two shared locks on the same table are compatible.
  lm.acquire(1, "users", LockMode::Shared);
  lm.acquire(2, "users", LockMode::Shared);

  // Exclusive while shared holders exist -> must time out (~3s) and throw.
  bool threw = false;
  auto start = std::chrono::steady_clock::now();
  try {
    lm.acquire(3, "users", LockMode::Exclusive);
  } catch (const std::runtime_error&) {
    threw = true;
  }
  auto elapsed = std::chrono::steady_clock::now() - start;
  CHECK(threw);
  CHECK(elapsed >= std::chrono::milliseconds(2800));  // honored the timeout

  // Once holders release, an exclusive lock can be granted promptly.
  lm.release_all(1);
  lm.release_all(2);
  lm.acquire(3, "users", LockMode::Exclusive);  // should not throw
  lm.release_all(3);

  // Lock upgrade: sole shared holder upgrades to exclusive.
  lm.acquire(4, "orders", LockMode::Shared);
  lm.acquire(4, "orders", LockMode::Exclusive);  // upgrade in place
  lm.release_all(4);

  // A blocked waiter proceeds as soon as the holder releases (no timeout).
  lm.acquire(5, "items", LockMode::Exclusive);
  std::thread releaser([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    lm.release_all(5);
  });
  lm.acquire(6, "items", LockMode::Exclusive);  // waits ~300ms then succeeds
  releaser.join();
  lm.release_all(6);
  std::cout << "[ok] LockManager: shared-compat / X-timeout / upgrade / wakeup\n";
}

static void testExecutor() {
  Catalog cat;
  LockManager lm;
  ExecContext ctx{&lm, /*txn=*/42};

  // users(id PK int, name text)
  Table* users = cat.createTable(
      "users", {{"id", ValueType::Int}, {"name", ValueType::Text}}, 0);
  // orders(uid int, item text)
  Table* orders = cat.createTable(
      "orders", {{"uid", ValueType::Int}, {"item", ValueType::Text}}, -1);

  for (int i = 1; i <= 5; ++i)
    users->insert({Value::Int(i), Value::Text("user" + std::to_string(i))});
  orders->insert({Value::Int(2), Value::Text("book")});
  orders->insert({Value::Int(2), Value::Text("pen")});
  orders->insert({Value::Int(4), Value::Text("lamp")});

  // SELECT * FROM users  (sequential scan)
  {
    TableScan scan(users, ctx);
    auto rows = execute(scan);
    CHECK(rows.size() == 5);
    lm.release_all(42);
  }

  // SELECT * FROM users WHERE id >= 2 AND id <= 4  (index range scan)
  {
    IndexScan iscan(users, 2, 4, ctx);
    auto rows = execute(iscan);
    CHECK(rows.size() == 3);
    CHECK(rows.front()[0].i == 2 && rows.back()[0].i == 4);
    lm.release_all(42);
  }

  // SELECT name FROM users WHERE id = 3  (filter + projection over a scan)
  {
    auto scan = std::make_unique<TableScan>(users, ctx);
    auto filt = std::make_unique<Filter>(
        std::move(scan), Predicate{0, CompareOp::Eq, Value::Int(3)});
    Projection proj(std::move(filt), {1});
    auto rows = execute(proj);
    CHECK(rows.size() == 1);
    CHECK(rows[0][0].s == "user3");
    lm.release_all(42);
  }

  // SELECT * FROM users JOIN orders ON users.id = orders.uid
  {
    auto u = std::make_unique<TableScan>(users, ctx);
    auto o = std::make_unique<TableScan>(orders, ctx);
    NestedLoopJoin join(std::move(u), std::move(o), /*left=*/0, /*right=*/0);
    auto rows = execute(join);
    CHECK(rows.size() == 3);  // user2 x2, user4 x1
    for (auto& r : rows) {
      CHECK(r.size() == 4);
      CHECK(r[0].i == r[2].i);  // join key matches
    }
    lm.release_all(42);
  }

  // INSERT INTO users VALUES (6, 'user6')
  {
    Insert ins(users, {Value::Int(6), Value::Text("user6")}, ctx);
    execute(ins);
    CHECK(ins.inserted() == 1);
    CHECK(users->size() == 6);
    CHECK(users->index().search(6).has_value());
    lm.release_all(42);
  }

  // DELETE FROM users WHERE id = 3  (tombstone via is_deleted)
  {
    auto scan = std::make_unique<TableScan>(users, ctx);
    auto filt = std::make_unique<Filter>(
        std::move(scan), Predicate{0, CompareOp::Eq, Value::Int(3)});
    Delete del(users, std::move(filt), ctx);
    execute(del);
    CHECK(del.deleted() == 1);
    lm.release_all(42);

    TableScan verify(users, ctx);
    auto rows = execute(verify);
    CHECK(rows.size() == 5);  // 6 inserted - 1 deleted
    for (auto& r : rows) CHECK(r[0].i != 3);
    CHECK(!users->index().search(3).has_value());
    lm.release_all(42);
  }
  std::cout << "[ok] Executor: scan/index/filter/project/join/insert/delete\n";
}

int main() {
  testBTree();
  testLockManager();
  testExecutor();
  std::cout << "\nAll Track 3 checks passed (" << g_checks << " assertions).\n";
  return 0;
}
