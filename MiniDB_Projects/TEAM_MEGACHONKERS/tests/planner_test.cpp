#include <gtest/gtest.h>
#include <vector>
#include <memory>
#include <filesystem>

#include "catalog/catalog.h"
#include "concurrency/lock_manager.h"
#include "concurrency/transaction_manager.h"
#include "execution/executor_context.h"
#include "execution/index_scan_executor.h"
#include "execution/filter_executor.h"
#include "execution/seq_scan_executor.h"
#include "parser/parser.h"
#include "parser/ast.h"
#include "planner/planner.h"

using namespace minidb;

class PlannerTest : public ::testing::Test {
protected:
    Catalog catalog;
    LockManager lock_manager;
    TransactionManager txn_manager{&lock_manager};

    void TearDown() override {
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(".", ec)) {
            auto ext = entry.path().extension();
            if (ext == ".log" || ext == ".sst") std::filesystem::remove(entry.path(), ec);
        }
    }

    // Plan + run a SELECT, returning all output rows.
    std::vector<Row> RunSelect(ExecutorContext& ctx, const std::string& sql) {
        Parser parser(sql);
        StmtPtr stmt = parser.Parse();
        auto* sel = dynamic_cast<SelectStatement*>(stmt.get());
        EXPECT_NE(sel, nullptr) << "Not a SELECT: " << sql;
        Planner planner(&ctx);
        auto plan = planner.PlanSelect(*sel);
        plan->Init();
        std::vector<Row> rows;
        Row r;
        while (plan->Next(&r)) rows.push_back(r);
        return rows;
    }

    int RunInsert(ExecutorContext& ctx, const std::string& sql) {
        Parser parser(sql);
        StmtPtr stmt = parser.Parse();
        auto* ins = dynamic_cast<InsertStatement*>(stmt.get());
        EXPECT_NE(ins, nullptr) << "Not an INSERT: " << sql;
        Planner planner(&ctx);
        auto plan = planner.PlanInsert(*ins);
        plan->Init();
        Row r;
        plan->Next(&r);
        return std::stoi(r.columns[0]);
    }

    int RunDelete(ExecutorContext& ctx, const std::string& sql) {
        Parser parser(sql);
        StmtPtr stmt = parser.Parse();
        auto* del = dynamic_cast<DeleteStatement*>(stmt.get());
        EXPECT_NE(del, nullptr) << "Not a DELETE: " << sql;
        Planner planner(&ctx);
        auto plan = planner.PlanDelete(*del);
        plan->Init();
        Row r;
        plan->Next(&r);
        return std::stoi(r.columns[0]);
    }
};

TEST_F(PlannerTest, ProjectionAndExpressionFilter) {
    catalog.CreateTable("emps", Schema({Column("id", TypeId::INTEGER, 4),
                                         Column("age", TypeId::INTEGER, 4),
                                         Column("role", TypeId::VARCHAR, 255)}));
    Transaction* txn = txn_manager.Begin();
    ExecutorContext ctx(&catalog, txn->GetTransactionId());

    RunInsert(ctx, "INSERT INTO emps VALUES (1, 25, 'eng'), (2, 41, 'mgr'), (3, 38, 'eng');");

    // Projection: only the 'role' column survives; predicate uses numeric '>'.
    auto rows = RunSelect(ctx, "SELECT role FROM emps WHERE age > 30;");
    ASSERT_EQ(rows.size(), 2u);
    for (const auto& r : rows) {
        EXPECT_EQ(r.columns.size(), 1u); // projected to a single column
    }

    // Boolean composition through the planner.
    auto eng_over_30 = RunSelect(ctx, "SELECT id FROM emps WHERE role = 'eng' AND age > 30;");
    ASSERT_EQ(eng_over_30.size(), 1u);
    EXPECT_EQ(eng_over_30[0].columns[0], "3");

    txn_manager.Commit(txn);
}

TEST_F(PlannerTest, IndexAwarePlanningChoosesIndexScan) {
    catalog.CreateTable("data", Schema({Column("id", TypeId::INTEGER, 4),
                                        Column("val", TypeId::VARCHAR, 255)}));
    catalog.CreateIndex("idx_id", "data", "id");

    Transaction* txn = txn_manager.Begin();
    ExecutorContext ctx(&catalog, txn->GetTransactionId());
    RunInsert(ctx, "INSERT INTO data VALUES (1, 'a'), (2, 'b'), (42, 'answer');");

    // WITH an index on id, an equality predicate should plan as an IndexScan.
    {
        Parser parser("SELECT * FROM data WHERE id = 42;");
        StmtPtr stmt = parser.Parse();
        Planner planner(&ctx);
        auto plan = planner.PlanSelect(*dynamic_cast<SelectStatement*>(stmt.get()));
        EXPECT_NE(dynamic_cast<IndexScanExecutor*>(plan.get()), nullptr)
            << "Expected an index-aware IndexScan plan.";
        plan->Init();
        Row r;
        ASSERT_TRUE(plan->Next(&r));
        EXPECT_EQ(r.columns[1], "answer");
    }

    txn_manager.Commit(txn);
}

TEST_F(PlannerTest, NoIndexFallsBackToSeqScanFilter) {
    catalog.CreateTable("data2", Schema({Column("id", TypeId::INTEGER, 4),
                                         Column("val", TypeId::VARCHAR, 255)}));
    Transaction* txn = txn_manager.Begin();
    ExecutorContext ctx(&catalog, txn->GetTransactionId());
    RunInsert(ctx, "INSERT INTO data2 VALUES (1, 'a'), (2, 'b');");

    Parser parser("SELECT * FROM data2 WHERE id = 2;");
    StmtPtr stmt = parser.Parse();
    Planner planner(&ctx);
    auto plan = planner.PlanSelect(*dynamic_cast<SelectStatement*>(stmt.get()));
    // No index => optimizer falls back to a FilterExecutor over a SeqScan.
    EXPECT_NE(dynamic_cast<FilterExecutor*>(plan.get()), nullptr)
        << "Expected SeqScan+Filter fallback when no index exists.";

    txn_manager.Commit(txn);
}

TEST_F(PlannerTest, JoinAndJoinProjection) {
    catalog.CreateTable("users", Schema({Column("user_id", TypeId::INTEGER, 4),
                                         Column("name", TypeId::VARCHAR, 255)}));
    catalog.CreateTable("orders", Schema({Column("order_id", TypeId::INTEGER, 4),
                                          Column("user_id", TypeId::INTEGER, 4),
                                          Column("item", TypeId::VARCHAR, 255)}));
    Transaction* txn = txn_manager.Begin();
    ExecutorContext ctx(&catalog, txn->GetTransactionId());
    RunInsert(ctx, "INSERT INTO users VALUES (1, 'Alice'), (2, 'Bob');");
    RunInsert(ctx, "INSERT INTO orders VALUES (101, 1, 'Laptop'), (102, 2, 'Phone'), (103, 1, 'Mouse');");

    // Full join: Alice has 2 orders, Bob has 1 => 3 matches, 5 columns each.
    auto joined = RunSelect(ctx,
        "SELECT * FROM users JOIN orders ON users.user_id = orders.user_id;");
    ASSERT_EQ(joined.size(), 3u);
    for (const auto& r : joined) EXPECT_EQ(r.columns.size(), 5u);

    // Projection over a join, with the condition written in reverse order.
    auto names = RunSelect(ctx,
        "SELECT users.name, orders.item FROM users JOIN orders ON orders.user_id = users.user_id;");
    ASSERT_EQ(names.size(), 3u);
    for (const auto& r : names) EXPECT_EQ(r.columns.size(), 2u);

    txn_manager.Commit(txn);
}

TEST_F(PlannerTest, DeleteThroughPlannerUpdatesScanAndIndex) {
    catalog.CreateTable("items", Schema({Column("id", TypeId::INTEGER, 4),
                                         Column("label", TypeId::VARCHAR, 255)}));
    catalog.CreateIndex("idx_items_id", "items", "id");

    Transaction* txn = txn_manager.Begin();
    ExecutorContext ctx(&catalog, txn->GetTransactionId());
    RunInsert(ctx, "INSERT INTO items VALUES (1, 'a'), (2, 'b'), (3, 'c');");

    int deleted = RunDelete(ctx, "DELETE FROM items WHERE id = 2;");
    EXPECT_EQ(deleted, 1);

    // SeqScan no longer sees id=2.
    auto remaining = RunSelect(ctx, "SELECT * FROM items;");
    ASSERT_EQ(remaining.size(), 2u);
    for (const auto& r : remaining) EXPECT_NE(r.columns[0], "2");

    // The index lookup for id=2 should now miss (IndexScan returns no row).
    Parser parser("SELECT * FROM items WHERE id = 2;");
    StmtPtr stmt = parser.Parse();
    Planner planner(&ctx);
    auto plan = planner.PlanSelect(*dynamic_cast<SelectStatement*>(stmt.get()));
    ASSERT_NE(dynamic_cast<IndexScanExecutor*>(plan.get()), nullptr);
    plan->Init();
    Row r;
    EXPECT_FALSE(plan->Next(&r));

    txn_manager.Commit(txn);
}

TEST_F(PlannerTest, UnknownTableAndColumnRaisePlanError) {
    Transaction* txn = txn_manager.Begin();
    ExecutorContext ctx(&catalog, txn->GetTransactionId());

    Parser p1("SELECT * FROM ghost;");
    auto s1 = p1.Parse();
    Planner planner(&ctx);
    EXPECT_THROW(planner.PlanSelect(*dynamic_cast<SelectStatement*>(s1.get())), PlanError);

    catalog.CreateTable("real_tbl", Schema({Column("id", TypeId::INTEGER, 4)}));
    Parser p2("SELECT missing_col FROM real_tbl;");
    auto s2 = p2.Parse();
    EXPECT_THROW(planner.PlanSelect(*dynamic_cast<SelectStatement*>(s2.get())), PlanError);

    txn_manager.Commit(txn);
}
