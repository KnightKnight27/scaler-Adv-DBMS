#include <gtest/gtest.h>
#include <vector>
#include <memory>
#include <filesystem>

#include "catalog/catalog.h"
#include "concurrency/lock_manager.h"
#include "concurrency/transaction_manager.h"
#include "execution/executor_context.h"
#include "execution/values_executor.h"
#include "execution/insert_executor.h"
#include "execution/seq_scan_executor.h"
#include "execution/filter_executor.h"
#include "execution/delete_executor.h"

using namespace minidb;

class FilterDeleteTest : public ::testing::Test {
protected:
    Catalog catalog;
    LockManager lock_manager;
    TransactionManager txn_manager{&lock_manager};

    void TearDown() override {
        // Best-effort cleanup; ignore files still locked by an open WAL handle
        // (see note in execution_test.cpp). Uses the non-throwing overload.
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(".", ec)) {
            if (entry.path().extension() == ".log") std::filesystem::remove(entry.path(), ec);
        }
    }
};

TEST_F(FilterDeleteTest, FilterExecutorTest) {
    Schema schema({Column("id", TypeId::INTEGER, 4), Column("role", TypeId::VARCHAR, 255)});
    TableMetadata* table = catalog.CreateTable("employees", schema);
    
    Transaction* txn = txn_manager.Begin();
    ExecutorContext context(&catalog, txn->GetTransactionId());

    // 1. Insert Data
    std::vector<Row> test_data = {
        Row{{"1", "Engineer"}},
        Row{{"2", "Manager"}},
        Row{{"3", "Engineer"}}
    };
    auto values_node = std::make_unique<ValuesExecutor>(&context, test_data);
    InsertExecutor insert_node(&context, std::move(values_node), table->oid);
    insert_node.Init();
    Row dummy; insert_node.Next(&dummy);

    // 2. Query: SELECT * FROM employees WHERE role = 'Engineer'
    auto scan_node = std::make_unique<SeqScanExecutor>(&context, table->oid);
    
    // Filter expects column 1 ("role") to equal "Engineer"
    FilterExecutor filter_node(&context, std::move(scan_node), 1, "Engineer");
    filter_node.Init();

    int match_count = 0;
    Row out_row;
    while (filter_node.Next(&out_row)) {
        EXPECT_EQ(out_row.columns[1], "Engineer");
        match_count++;
    }

    // Only Alice (1) and Charlie (3) are Engineers
    EXPECT_EQ(match_count, 2);
    txn_manager.Commit(txn);
}

TEST_F(FilterDeleteTest, DeleteExecutorTest) {
    Schema schema({Column("id", TypeId::INTEGER, 4)});
    TableMetadata* table = catalog.CreateTable("items", schema);
    
    Transaction* txn = txn_manager.Begin();
    ExecutorContext context(&catalog, txn->GetTransactionId());

    // 1. Insert Data (1, 2, 3)
    std::vector<Row> test_data = { Row{{"1"}}, Row{{"2"}}, Row{{"3"}} };
    auto values_node = std::make_unique<ValuesExecutor>(&context, test_data);
    InsertExecutor insert_node(&context, std::move(values_node), table->oid);
    insert_node.Init();
    Row dummy; insert_node.Next(&dummy);

    // 2. Query: DELETE FROM items WHERE id = '2'
    // Step A: Provide the row to delete
    std::vector<Row> delete_target = { Row{{"2"}} };
    auto delete_values = std::make_unique<ValuesExecutor>(&context, delete_target);
    
    // Step B: Execute the Delete
    DeleteExecutor delete_node(&context, std::move(delete_values), table->oid);
    delete_node.Init();
    Row result;
    EXPECT_TRUE(delete_node.Next(&result));
    EXPECT_EQ(result.columns[0], "1"); // 1 row deleted

    // 3. Verify SeqScan skips the deleted row
    SeqScanExecutor scan_node(&context, table->oid);
    scan_node.Init();

    int active_rows = 0;
    Row out_row;
    while (scan_node.Next(&out_row)) {
        EXPECT_NE(out_row.columns[0], "2"); // ID 2 should NEVER appear
        active_rows++;
    }

    // Only ID 1 and 3 remain
    EXPECT_EQ(active_rows, 2);
    txn_manager.Commit(txn);
}