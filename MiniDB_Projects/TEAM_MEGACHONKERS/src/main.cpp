#include <iostream>
#include <vector>
#include <memory>

#include "catalog/catalog.h"
#include "concurrency/lock_manager.h"
#include "concurrency/transaction_manager.h"
#include "execution/executor_context.h"
#include "execution/values_executor.h"
#include "execution/insert_executor.h"
#include "execution/seq_scan_executor.h"
#include "common/logger.h"

using namespace minidb;

int main() {
    LOG_INFO("Starting MiniDB (LSM-Tree Engine)...");

    // 1. Boot up the global database managers
    Catalog catalog;
    LockManager lock_manager;
    TransactionManager txn_manager(&lock_manager);

    // 2. Define a Schema for our "users" table: (id INT, name VARCHAR)
    std::vector<Column> columns = {
        Column("id", TypeId::INTEGER, 4),
        Column("name", TypeId::VARCHAR, 255)
    };
    Schema schema(columns);

    // 3. Create the Table
    TableMetadata* table = catalog.CreateTable("users", schema);
    if (!table) {
        LOG_ERROR("Failed to create table.");
        return 1;
    }

    // 4. Begin a Transaction
    Transaction* txn = txn_manager.Begin();
    ExecutorContext context(&catalog, txn->GetTransactionId());
    LOG_INFO("Started Transaction: " + std::to_string(txn->GetTransactionId()));

    // 5. Create some dummy data (Simulating: INSERT INTO users VALUES (1, 'Alice'), (2, 'Bob'))
    std::vector<Row> raw_data = {
        Row{ {"1", "Alice"} },
        Row{ {"2", "Bob"} }
    };

    // 6. Build and Run the Execution Pipeline: Values -> Insert
    auto values_node = std::make_unique<ValuesExecutor>(&context, raw_data);
    InsertExecutor insert_node(&context, std::move(values_node), table->oid);
    
    insert_node.Init();
    Row result_row;
    if (insert_node.Next(&result_row)) {
        LOG_INFO("Successfully inserted rows.");
    }

    // 7. Commit the Write Transaction (Releases any locks)
    txn_manager.Commit(txn);
    LOG_INFO("Transaction Committed.");

    // 8. Read the data back (Simulating: SELECT * FROM users)
    LOG_INFO("Executing: SELECT * FROM users");
    
    // Start a new read transaction
    Transaction* read_txn = txn_manager.Begin();
    ExecutorContext read_context(&catalog, read_txn->GetTransactionId());

    SeqScanExecutor scan_node(&read_context, table->oid);
    scan_node.Init();

    Row output_row;
    while (scan_node.Next(&output_row)) {
        std::cout << " -> Row: [ID: " << output_row.columns[0] 
                  << ", Name: " << output_row.columns[1] << "]\n";
    }

    txn_manager.Commit(read_txn);
    
    LOG_INFO("MiniDB Shutdown cleanly.");
    return 0;
}