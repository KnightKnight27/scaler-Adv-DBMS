#include "test_support.h"
#include "catalog/catalog.h"
#include "execution/executor.h"
#include "optimizer/optimizer.h"
#include "recovery/recovery_manager.h"
#include "recovery/wal.h"
#include "transaction/txn_manager.h"

using namespace minidb;

void TestRecovery() {
    RemoveFile("test_source.db");
    RemoveFile("test_recovered.db");
    RemoveFile("test_recovery.log");
    Schema schema{{"id", ColumnType::INT}, {"value", ColumnType::STRING}};

    WALManager wal("test_recovery.log");
    {
        DiskManager disk("test_source.db");
        BufferPoolManager pool(8, &disk);
        Catalog catalog;
        catalog.CreateTable("events", schema, &pool);
        Optimizer optimizer;
        LockManager locks;
        TransactionManager txns(&locks, &wal);
        Executor executor(&catalog, &optimizer, &txns);

        executor.Execute("INSERT INTO events VALUES (1, 'keep')");
        Transaction* aborted = txns.Begin();
        executor.Execute("INSERT INTO events VALUES (2, 'ignore')", aborted);
        txns.Abort(aborted);
        executor.Execute("INSERT INTO events VALUES (3, 'delete')");
        executor.Execute("DELETE FROM events WHERE id = 3");
    }

    {
        DiskManager disk("test_recovered.db");
        BufferPoolManager pool(8, &disk);
        Catalog catalog;
        TableInfo* table = catalog.CreateTable("events", schema, &pool);
        RecoveryResult result = RecoveryManager(&catalog, &wal).Recover();
        Check(result.redone == 3, "unexpected REDO count");
        Check(result.ignored == 1, "uncommitted WAL record was not ignored");
        Check(table->GetTupleCount() == 1, "recovered row count is incorrect");
        Check(table->primary_index->Search(1).has_value(), "committed row missing");
        Check(!table->primary_index->Search(2), "aborted row was recovered");
        Check(!table->primary_index->Search(3), "committed delete was not recovered");
    }

    RemoveFile("test_source.db");
    RemoveFile("test_recovered.db");
    RemoveFile("test_recovery.log");
}
