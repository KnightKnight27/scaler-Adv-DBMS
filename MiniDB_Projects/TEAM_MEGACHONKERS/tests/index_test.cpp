#include <gtest/gtest.h>
#include <vector>
#include <memory>

#include "catalog/catalog.h"
#include "execution/executor_context.h"
#include "execution/index_scan_executor.h"
#include "storage/btree/bplus_tree.h"
#include "concurrency/lock_manager.h"
#include "concurrency/transaction_manager.h"

using namespace minidb;

class IndexTest : public ::testing::Test {
protected:
    Catalog catalog;
    LockManager lock_manager;
    TransactionManager txn_manager{&lock_manager};
};

TEST_F(IndexTest, BTreeDeleteAndIndexScan) {
    Schema schema({Column("id", TypeId::INTEGER, 4), Column("name", TypeId::VARCHAR, 255)});
    TableMetadata* table = catalog.CreateTable("users", schema);
    
    Transaction* txn = txn_manager.Begin();
    ExecutorContext context(&catalog, txn->GetTransactionId());

    // 1. Initialize our B+ Tree Secondary Index
    BPlusTree index(5);
    
    // 2. Insert Data into the Index
    Row row1{{"1", "Alice"}};
    Row row2{{"2", "Bob"}};
    Row row3{{"3", "Charlie"}};
    
    index.Insert("1", row1.Serialize());
    index.Insert("2", row2.Serialize());
    index.Insert("3", row3.Serialize());

    // 3. Test B+ Tree Delete Requirement
    index.Delete("2"); // Delete Bob
    
    EXPECT_FALSE(index.Search("2").has_value()); // Bob should be gone
    EXPECT_TRUE(index.Search("1").has_value());  // Alice should remain

    // 4. Demonstrate Index Utilization during Query Execution Requirement
    // Query: SELECT * FROM users WHERE id = '3' (Using IndexScan instead of SeqScan)
    IndexScanExecutor index_scan(&context, table->oid, &index, "3");
    index_scan.Init();

    Row result_row;
    bool found = index_scan.Next(&result_row);
    
    EXPECT_TRUE(found);
    EXPECT_EQ(result_row.columns[1], "Charlie");

    // Should return false on the second call (only 1 point lookup match)
    EXPECT_FALSE(index_scan.Next(&result_row));

    txn_manager.Commit(txn);
}