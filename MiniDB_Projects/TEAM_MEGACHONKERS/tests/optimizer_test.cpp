#include <gtest/gtest.h>
#include <memory>

#include "catalog/catalog.h"
#include "execution/executor_context.h"
#include "optimizer/optimizer.h"
#include "execution/index_scan_executor.h"
#include "execution/filter_executor.h"
#include "storage/btree/bplus_tree.h"
#include "concurrency/lock_manager.h"
#include "concurrency/transaction_manager.h"

using namespace minidb;

class OptimizerTest : public ::testing::Test {
protected:
    Catalog catalog;
    LockManager lock_manager;
    TransactionManager txn_manager{&lock_manager};
};

TEST_F(OptimizerTest, PointLookupPlanSelection) {
    Schema schema({Column("id", TypeId::INTEGER, 4), Column("value", TypeId::VARCHAR, 255)});
    TableMetadata* table = catalog.CreateTable("data_table", schema);
    
    Transaction* txn = txn_manager.Begin();
    ExecutorContext context(&catalog, txn->GetTransactionId());
    
    Optimizer optimizer(&context);
    BPlusTree btree_index(5); // Our secondary index

    // =========================================================================
    // Scenario 1: Large table WITH a valid index
    // The optimizer should calculate O(log N) < O(N) and build an IndexScan plan
    // =========================================================================
    auto optimized_plan_with_index = optimizer.OptimizePointLookup(
        table->oid, 
        "42", 
        0,            // filter on column 0
        &btree_index, // pass the index
        10000         // estimated rows: 10,000
    );

    // Verify the physical operator chosen is specifically an IndexScanExecutor
    auto* index_scan_op = dynamic_cast<IndexScanExecutor*>(optimized_plan_with_index.get());
    EXPECT_NE(index_scan_op, nullptr) << "Optimizer failed to choose IndexScan when it was cheaper!";

    // =========================================================================
    // Scenario 2: Large table WITHOUT an index
    // The optimizer must gracefully fall back to a SeqScan + Filter pipeline
    // =========================================================================
    auto optimized_plan_no_index = optimizer.OptimizePointLookup(
        table->oid, 
        "42", 
        0,            // filter on column 0
        nullptr,      // NO index available
        10000         // estimated rows: 10,000
    );

    // Verify the physical operator chosen is a FilterExecutor (which wraps a SeqScan)
    auto* filter_op = dynamic_cast<FilterExecutor*>(optimized_plan_no_index.get());
    EXPECT_NE(filter_op, nullptr) << "Optimizer failed to fallback to SeqScan+Filter when no index was available!";

    txn_manager.Commit(txn);
}