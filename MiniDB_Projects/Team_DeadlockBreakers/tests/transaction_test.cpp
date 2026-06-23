#include "test_support.h"
#include "catalog/catalog.h"
#include "execution/executor.h"
#include "optimizer/optimizer.h"
#include "recovery/wal.h"
#include "transaction/txn_manager.h"

#include <thread>

using namespace minidb;

void TestTransactionsAndDeadlock() {
    RemoveFile("test_txn.db");
    RemoveFile("test_txn_aux.db");
    RemoveFile("test_txn.log");
    DiskManager disk("test_txn.db");
    BufferPoolManager pool(8, &disk);
    DiskManager aux_disk("test_txn_aux.db");
    BufferPoolManager aux_pool(8, &aux_disk);
    Catalog catalog;
    catalog.CreateTable("accounts", {{"id", ColumnType::INT},
                        {"name", ColumnType::STRING}}, &pool);
    catalog.CreateTable("labels", {{"id", ColumnType::INT},
                        {"account_id", ColumnType::INT}}, &aux_pool);
    Optimizer optimizer;
    WALManager wal("test_txn.log");
    LockManager locks;
    TransactionManager txns(&locks, &wal);
    Executor executor(&catalog, &optimizer, &txns);

    Transaction* aborted = txns.Begin();
    executor.Execute("INSERT INTO accounts VALUES (1, 'uncommitted')", aborted);
    txns.Abort(aborted);
    Check(executor.Execute("SELECT * FROM accounts").empty(),
          "aborted insert became visible");

    Transaction* committed = txns.Begin();
    executor.Execute("INSERT INTO accounts VALUES (1, 'committed')", committed);
    Check(txns.Commit(committed), "commit failed");
    Check(executor.Execute("SELECT * FROM accounts").size() == 1,
          "committed insert is missing");
    executor.Execute("INSERT INTO labels VALUES (10, 1)");
    Check(executor.Execute("SELECT * FROM accounts JOIN labels ON "
                           "accounts.id = labels.account_id").size() == 1,
          "SQL join failed");

    Transaction* deleted = txns.Begin();
    executor.Execute("DELETE FROM accounts WHERE id = 1", deleted);
    txns.Abort(deleted);
    Check(executor.Execute("SELECT * FROM accounts").size() == 1,
          "aborted delete removed data");

    Transaction* first = txns.Begin();
    Transaction* second = txns.Begin();
    Check(locks.LockExclusive(first, "left"), "first lock failed");
    Check(locks.LockExclusive(second, "right"), "second lock failed");
    bool first_cross = true, second_cross = true;
    std::thread a([&] { first_cross = locks.LockExclusive(first, "right"); });
    std::thread b([&] { second_cross = locks.LockExclusive(second, "left"); });
    a.join(); b.join();
    Check(!first_cross && !second_cross, "deadlock timeout was not detected");
    txns.Abort(first);
    txns.Abort(second);
}
