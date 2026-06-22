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
#include "execution/join_executor.h"

using namespace minidb;

class ExecutionTest : public ::testing::Test {
protected:
    Catalog catalog;
    LockManager lock_manager;
    TransactionManager txn_manager{&lock_manager};

    void TearDown() override {
        // Clean up any stray SSTables or WAL files created during executor tests
        for (const auto& entry : std::filesystem::directory_iterator(".")) {
            if (entry.path().extension() == ".sst" || entry.path().extension() == ".log") {
                std::filesystem::remove(entry.path());
            }
        }
    }
};

TEST_F(ExecutionTest, InsertAndSeqScan) {
    Schema schema({
        Column("id", TypeId::INTEGER, 4),
        Column("name", TypeId::VARCHAR, 255)
    });
    TableMetadata* table = catalog.CreateTable("employees", schema);
    
    Transaction* txn = txn_manager.Begin();
    ExecutorContext context(&catalog, txn->GetTransactionId());

    // 1. Setup Data
    std::vector<Row> test_data = {
        Row{{"1", "Alice"}},
        Row{{"2", "Bob"}},
        Row{{"3", "Charlie"}}
    };

    // 2. Execute Insert
    auto values_node = std::make_unique<ValuesExecutor>(&context, test_data);
    InsertExecutor insert_node(&context, std::move(values_node), table->oid);
    insert_node.Init();
    
    Row result_row;
    EXPECT_TRUE(insert_node.Next(&result_row));
    EXPECT_EQ(result_row.columns[0], "3"); // 3 rows inserted

    // 3. Execute SeqScan
    SeqScanExecutor scan_node(&context, table->oid);
    scan_node.Init();

    Row out_row;
    int count = 0;
    while (scan_node.Next(&out_row)) {
        count++;
    }
    
    EXPECT_EQ(count, 3);
    txn_manager.Commit(txn);
}

TEST_F(ExecutionTest, NestedLoopJoinTest) {
    // 1. Create Users Table
    Schema users_schema({ Column("user_id", TypeId::INTEGER, 4), Column("name", TypeId::VARCHAR, 255) });
    TableMetadata* users_table = catalog.CreateTable("users", users_schema);
    
    // 2. Create Orders Table
    Schema orders_schema({ Column("order_id", TypeId::INTEGER, 4), Column("user_id", TypeId::INTEGER, 4), Column("item", TypeId::VARCHAR, 255) });
    TableMetadata* orders_table = catalog.CreateTable("orders", orders_schema);

    Transaction* txn = txn_manager.Begin();
    ExecutorContext context(&catalog, txn->GetTransactionId());

    // 3. Populate Users: (1, 'Alice'), (2, 'Bob')
    auto users_values = std::make_unique<ValuesExecutor>(&context, std::vector<Row>{ Row{{"1", "Alice"}}, Row{{"2", "Bob"}} });
    InsertExecutor insert_users(&context, std::move(users_values), users_table->oid);
    insert_users.Init();
    Row dummy; insert_users.Next(&dummy);

    // 4. Populate Orders: (101, 1, 'Laptop'), (102, 2, 'Phone'), (103, 1, 'Mouse')
    auto orders_values = std::make_unique<ValuesExecutor>(&context, std::vector<Row>{ 
        Row{{"101", "1", "Laptop"}}, Row{{"102", "2", "Phone"}}, Row{{"103", "1", "Mouse"}} 
    });
    InsertExecutor insert_orders(&context, std::move(orders_values), orders_table->oid);
    insert_orders.Init();
    insert_orders.Next(&dummy);

    // 5. Setup Nested Loop Join
    // Query: SELECT * FROM users JOIN orders ON users.user_id = orders.user_id
    // left_col_idx = 0 (users.user_id), right_col_idx = 1 (orders.user_id)
    auto left_scan = std::make_unique<SeqScanExecutor>(&context, users_table->oid);
    auto right_scan = std::make_unique<SeqScanExecutor>(&context, orders_table->oid);
    
    NestedLoopJoinExecutor join_node(&context, std::move(left_scan), std::move(right_scan), 0, 1);
    join_node.Init();

    int match_count = 0;
    Row out_row;
    while (join_node.Next(&out_row)) {
        match_count++;
        // The output schema should be concatenated: [user_id, name, order_id, order_user_id, item]
        EXPECT_EQ(out_row.columns.size(), 5);
        EXPECT_EQ(out_row.columns[0], out_row.columns[3]); // user_id must match order_user_id
    }

    // Alice has 2 orders, Bob has 1 order. Total join matches should be 3.
    EXPECT_EQ(match_count, 3);
    
    txn_manager.Commit(txn);
}