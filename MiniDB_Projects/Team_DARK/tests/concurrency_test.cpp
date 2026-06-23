#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "concurrency/lock_manager.h"
#include "concurrency/transaction_manager.h"
#include "storage/buffer_pool_manager.h"
#include "storage/disk_manager.h"
#include "storage/page.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr auto kWaitTimeout = std::chrono::seconds(5);

std::string TempDbPath(const char* suffix) {
    return std::string("/tmp/minidb_test_") + suffix + "_" +
           std::to_string(static_cast<unsigned long>(std::time(nullptr))) + ".db";
}

void RemoveFile(const std::string& path) {
    std::remove(path.c_str());
}

}  // namespace

TEST_CASE("Row version header layout in page") {
    char page_data[minidb::PAGE_SIZE]{};
    minidb::Page page(page_data);

    const minidb::RowVersionHeader header =
        minidb::Page::MakeDefaultRowVersionHeader(42);
    page.SetRowVersionHeader(100, header);

    const minidb::RowVersionHeader read = page.GetRowVersionHeader(100);
    CHECK(read.xmin == 42);
    CHECK(read.xmax == minidb::INVALID_VERSION_TID);
    CHECK(read.prev_version_tid == minidb::INVALID_VERSION_TID);
    CHECK(minidb::ROW_VERSION_HEADER_SIZE == 24);
}

TEST_CASE("MVCC snapshot isolation") {
    minidb::TransactionManager tm;

    const minidb::TxID t1 = tm.Begin();
    tm.Insert(t1, "balance", "1000");
    tm.Commit(t1);

    const minidb::TxID t2 = tm.Begin();
    const minidb::TxID t3 = tm.Begin();

    tm.Update(t3, "balance", "2000");
    tm.Commit(t3);

    const auto snapshot_value = tm.Read(t2, "balance");
    REQUIRE(snapshot_value.has_value());
    CHECK(*snapshot_value == "1000");

    tm.Commit(t2);
}

TEST_CASE("Readers bypass locking — concurrent reads") {
    minidb::TransactionManager tm;

    const minidb::TxID writer = tm.Begin();
    tm.Insert(writer, "counter", "42");
    tm.Commit(writer);

    std::atomic<int> readers_completed{0};
    std::vector<std::thread> readers;
    readers.reserve(8);

    for (int i = 0; i < 8; ++i) {
        readers.emplace_back([&]() {
            const minidb::TxID reader = tm.Begin();
            const auto value = tm.Read(reader, "counter");
            if (value.has_value() && *value == "42") {
                readers_completed.fetch_add(1);
            }
            tm.Commit(reader);
        });
    }

    for (std::thread& thread : readers) {
        thread.join();
    }

    CHECK(readers_completed == 8);
}

TEST_CASE("Write-write exclusive locking serializes updates") {
    minidb::TransactionManager tm;

    const minidb::TxID setup = tm.Begin();
    tm.Insert(setup, "account", "0");
    tm.Commit(setup);

    std::mutex order_mutex;
    std::vector<int> commit_order;

    const minidb::TxID t1 = tm.Begin();
    const minidb::TxID t2 = tm.Begin();

    std::thread writer1([&]() {
        tm.Update(t1, "account", "first");
        {
            std::lock_guard<std::mutex> lock(order_mutex);
            commit_order.push_back(1);
        }
        tm.Commit(t1);
    });

    std::thread writer2([&]() {
        tm.Update(t2, "account", "second");
        {
            std::lock_guard<std::mutex> lock(order_mutex);
            commit_order.push_back(2);
        }
        tm.Commit(t2);
    });

    writer1.join();
    writer2.join();

    CHECK(commit_order.size() == 2);

    const minidb::TxID reader = tm.Begin();
    const auto final_value = tm.Read(reader, "account");
    REQUIRE(final_value.has_value());
    const bool valid_final = (*final_value == "first" || *final_value == "second");
    CHECK(valid_final);
    tm.Commit(reader);
}

TEST_CASE("Deadlock detection aborts younger transaction") {
    minidb::TransactionManager tm;
    minidb::LockManager& lm = tm.GetLockManager();

    const minidb::TxID setup_a = tm.Begin();
    tm.Insert(setup_a, "A", "val_a");
    tm.Commit(setup_a);

    const minidb::TxID setup_b = tm.Begin();
    tm.Insert(setup_b, "B", "val_b");
    tm.Commit(setup_b);

    const minidb::TxID t8 = tm.Begin();
    const minidb::TxID t9 = tm.Begin();

    lm.AcquireLock("A", t8, minidb::LockMode::EXCLUSIVE, false);
    lm.AcquireLock("B", t9, minidb::LockMode::EXCLUSIVE, false);

    std::atomic<bool> deadlock_detected{false};
    std::atomic<minidb::TxID> victim_xid{0};

    std::thread waiter([&]() {
        try {
            lm.AcquireLock("B", t8, minidb::LockMode::EXCLUSIVE, false);
            tm.Commit(t8);
        } catch (const minidb::DeadlockException& ex) {
            deadlock_detected.store(true);
            victim_xid.store(ex.VictimXid());
            tm.Abort(t8);
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    try {
        lm.AcquireLock("A", t9, minidb::LockMode::EXCLUSIVE, false);
        tm.Commit(t9);
    } catch (const minidb::DeadlockException& ex) {
        deadlock_detected.store(true);
        victim_xid.store(ex.VictimXid());
        tm.Abort(t9);
    }

    const bool joined = [&]() {
        using clock = std::chrono::steady_clock;
        const auto deadline = clock::now() + kWaitTimeout;
        while (clock::now() < deadline) {
            if (waiter.joinable()) {
                waiter.join();
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    }();

    CHECK(joined);
    CHECK(deadlock_detected.load());
    CHECK(victim_xid.load() == std::max(t8, t9));
    const bool one_aborted = tm.GetStatus(t8) == minidb::TxStatus::ABORTED ||
                             tm.GetStatus(t9) == minidb::TxStatus::ABORTED;
    const bool one_committed = tm.GetStatus(t8) == minidb::TxStatus::COMMITTED ||
                               tm.GetStatus(t9) == minidb::TxStatus::COMMITTED;
    CHECK(one_aborted);
    CHECK(one_committed);
}

TEST_CASE("Deadlock aborts remote victim without hanging") {
    minidb::LockManager lm;

    const minidb::TxID older = 2;
    const minidb::TxID younger = 1;

    lm.AcquireLock("A", older, minidb::LockMode::EXCLUSIVE, false);
    lm.AcquireLock("B", younger, minidb::LockMode::EXCLUSIVE, false);

    std::atomic<bool> older_aborted{false};
    std::atomic<bool> younger_acquired{false};

    std::thread older_waiter([&]() {
        try {
            lm.AcquireLock("B", older, minidb::LockMode::EXCLUSIVE, false);
        } catch (const minidb::DeadlockException& ex) {
            older_aborted.store(true);
            CHECK(ex.VictimXid() == older);
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::thread younger_waiter([&]() {
        lm.AcquireLock("A", younger, minidb::LockMode::EXCLUSIVE, false);
        younger_acquired.store(true);
        lm.ReleaseLocks(younger);
    });

    const bool older_joined = [&]() {
        using clock = std::chrono::steady_clock;
        const auto deadline = clock::now() + kWaitTimeout;
        while (clock::now() < deadline) {
            if (!older_waiter.joinable()) {
                return true;
            }
            if (older_aborted.load() && younger_acquired.load()) {
                older_waiter.join();
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    }();

    younger_waiter.join();
    lm.ReleaseLocks(older);

    CHECK(older_joined);
    CHECK(older_aborted.load());
    CHECK(younger_acquired.load());
}

TEST_CASE("Abort rolls back uncommitted writes") {
    minidb::TransactionManager tm;

    const minidb::TxID t1 = tm.Begin();
    tm.Insert(t1, "item", "original");
    tm.Commit(t1);

    const minidb::TxID t2 = tm.Begin();
    tm.Update(t2, "item", "aborted_write");
    tm.Abort(t2);

    const minidb::TxID t3 = tm.Begin();
    const auto value = tm.Read(t3, "item");
    REQUIRE(value.has_value());
    CHECK(*value == "original");
    tm.Commit(t3);
}

TEST_CASE("Strict 2PL rejects lock acquisition in shrinking phase") {
    minidb::TransactionManager tm;
    minidb::LockManager& lm = tm.GetLockManager();

    const minidb::TxID xid = tm.Begin();
    lm.AcquireLock("key", xid, minidb::LockMode::EXCLUSIVE, false);
    lm.ReleaseLocks(xid);

    CHECK_THROWS_AS(lm.AcquireLock("key", xid, minidb::LockMode::EXCLUSIVE, true),
                    std::runtime_error);
    tm.Abort(xid);
}

TEST_CASE("Committed transaction visibility") {
    minidb::TransactionManager tm;

    CHECK_FALSE(tm.IsCommitted(999));

    const minidb::TxID xid = tm.Begin();
    CHECK(tm.GetStatus(xid) == minidb::TxStatus::ACTIVE);
    tm.Insert(xid, "k", "v");
    tm.Commit(xid);

    CHECK(tm.IsCommitted(xid));
    CHECK_FALSE(tm.IsAborted(xid));
}

TEST_CASE("MVCC rows stored in buffer pool pages") {
    const std::string db_path = TempDbPath("mvcc_heap");
    minidb::DiskManager disk(db_path);
    minidb::BufferPoolManager bpm(&disk, 8);
    minidb::TransactionManager tm(&bpm);

    const minidb::TxID xid = tm.Begin();
    tm.Insert(xid, "row_key", "payload");
    tm.Commit(xid);

    char* page_data = bpm.FetchPage(0);
    minidb::Page page(page_data);
    const minidb::PageHeader header = page.GetHeader();
    CHECK(header.slot_count >= 1);

    const minidb::SlotEntry slot = page.GetSlot(0);
    const minidb::RowVersionHeader row_header = page.GetRowVersionHeader(slot.offset);
    CHECK(row_header.xmin == xid);
    CHECK(row_header.xmax == minidb::INVALID_VERSION_TID);

    std::string stored_value(slot.length - minidb::ROW_VERSION_HEADER_SIZE, '\0');
    std::memcpy(stored_value.data(), page_data + slot.offset + minidb::ROW_VERSION_HEADER_SIZE,
                stored_value.size());
    CHECK(stored_value == "row_key\tpayload");

    bpm.UnpinPage(0);
    RemoveFile(db_path);
}

TEST_CASE("ReleaseLocks releases only transaction-owned keys") {
    minidb::LockManager lm;

    const minidb::TxID t1 = 1;
    const minidb::TxID t2 = 2;

    lm.AcquireLock("A", t1, minidb::LockMode::EXCLUSIVE, false);
    lm.AcquireLock("B", t1, minidb::LockMode::EXCLUSIVE, false);
    lm.AcquireLock("C", t2, minidb::LockMode::EXCLUSIVE, false);

    lm.ReleaseLocks(t1);

    std::atomic<bool> acquired_a{false};
    std::thread acquire_a([&]() {
        lm.AcquireLock("A", 3, minidb::LockMode::EXCLUSIVE, false);
        acquired_a.store(true);
        lm.ReleaseLocks(3);
    });
    acquire_a.join();
    CHECK(acquired_a.load());

    std::atomic<bool> acquired_c{false};
    std::thread acquire_c([&]() {
        lm.AcquireLock("C", 4, minidb::LockMode::EXCLUSIVE, false);
        acquired_c.store(true);
        lm.ReleaseLocks(4);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK_FALSE(acquired_c.load());

    lm.ReleaseLocks(t2);
    acquire_c.join();
    CHECK(acquired_c.load());
}

TEST_CASE("MVCC snapshot isolation respects active transaction lists") {
    minidb::TransactionManager tm;

    const minidb::TxID t1 = tm.Begin();
    tm.Insert(t1, "k1", "v1");
    tm.Commit(t1);

    // Start t2. It sees "v1".
    const minidb::TxID t2 = tm.Begin();

    // Start concurrent t3.
    const minidb::TxID t3 = tm.Begin();
    tm.Update(t3, "k1", "v3");
    // t3 commits now.
    tm.Commit(t3);

    // t2 reads "k1". Since t3 committed AFTER t2 started (and t3 was active or started after t2),
    // t2 MUST still see "v1", not "v3".
    const auto val2 = tm.Read(t2, "k1");
    REQUIRE(val2.has_value());
    CHECK(*val2 == "v1");

    // t4 starts after t3 committed. It must see "v3".
    const minidb::TxID t4 = tm.Begin();
    const auto val4 = tm.Read(t4, "k1");
    REQUIRE(val4.has_value());
    CHECK(*val4 == "v3");

    tm.Commit(t2);
    tm.Commit(t4);
}

TEST_CASE("MVCC version garbage collection prunes dead versions") {
    minidb::TransactionManager tm;

    const minidb::TxID t1 = tm.Begin();
    tm.Insert(t1, "gc_key", "v1");
    tm.Commit(t1);

    const minidb::TxID t2 = tm.Begin();
    tm.Update(t2, "gc_key", "v2");
    tm.Commit(t2);

    const minidb::TxID t3 = tm.Begin();
    tm.Update(t3, "gc_key", "v3");
    tm.Commit(t3);

    const std::vector<minidb::StoredRowVersion> versions =
        tm.GetTableHeap().GetVersions("gc_key");
    REQUIRE(versions.size() == 1);
    CHECK(versions[0].value == "v3");
}

TEST_CASE("MVCC GC respects active snapshot horizon") {
    minidb::TransactionManager tm;

    const minidb::TxID t1 = tm.Begin();
    tm.Insert(t1, "gc_key", "v1");
    tm.Commit(t1);

    const minidb::TxID t2 = tm.Begin();
    const minidb::TxID t3 = tm.Begin();
    tm.Update(t3, "gc_key", "v2");
    tm.Commit(t3);

    CHECK(tm.GetTableHeap().GetVersions("gc_key").size() == 2);

    tm.Commit(t2);

    const minidb::TxID t4 = tm.Begin();
    tm.Commit(t4);

    const std::vector<minidb::StoredRowVersion> versions =
        tm.GetTableHeap().GetVersions("gc_key");
    REQUIRE(versions.size() == 1);
    CHECK(versions[0].value == "v2");
}
