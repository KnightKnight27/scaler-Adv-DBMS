#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "execution/executor.h"
#include "execution/optimizer.h"
#include "parser/lexer.h"
#include "parser/parser.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <string>

using namespace minidb;

TEST_CASE("Lexer tokenizes SELECT with OR and parentheses") {
    Lexer lexer("SELECT name FROM users WHERE (age > 20 OR id < 5)");
    const std::vector<Token> tokens = lexer.Tokenize();

    REQUIRE(tokens.size() >= 10);
    CHECK(tokens[0].type == TokenType::SELECT);
    CHECK(tokens[tokens.size() - 1].type == TokenType::END);
}

TEST_CASE("Parser builds SELECT AST with OR and AND") {
    Lexer lexer("SELECT name FROM users WHERE age > 20 OR id < 5");
    Parser parser(lexer.Tokenize());
    const Statement statement = parser.Parse();

    REQUIRE(statement.type == StatementType::SELECT);
    CHECK(statement.select.column == "name");
    CHECK(statement.select.table == "users");
    REQUIRE(statement.select.where != nullptr);

    const auto* root = dynamic_cast<const BinaryExpr*>(statement.select.where.get());
    REQUIRE(root != nullptr);
    CHECK(root->op == "OR");
}

TEST_CASE("Execute SELECT with OR filter") {
    TransactionManager tm;
    QueryEngine engine(&tm);
    engine.SeedDemoData();

    const QueryResult result =
        engine.ExecuteSql("SELECT name FROM users WHERE age > 20 OR id < 5");

    REQUIRE(result.rows.size() == 4);
    CHECK(std::find(result.rows.begin(), result.rows.end(), "Kartik") != result.rows.end());
    CHECK(std::find(result.rows.begin(), result.rows.end(), "Krishank") != result.rows.end());
    CHECK(std::find(result.rows.begin(), result.rows.end(), "Sandip") != result.rows.end());
    CHECK(std::find(result.rows.begin(), result.rows.end(), "Nitish") != result.rows.end());
}

TEST_CASE("Optimizer selects IndexScan for indexed column filter") {
    TransactionManager tm;
    QueryEngine engine(&tm);
    engine.SeedDemoData();

    Lexer lexer("SELECT name FROM users WHERE id = 3");
    Parser parser(lexer.Tokenize());
    const Statement statement = parser.Parse();
    const std::unique_ptr<PlanNode> plan = engine.OptimizeStatement(statement);

    CHECK(engine.PlanUsesIndexScan(*plan));
    CHECK(engine.FindAccessPlanType(*plan) == PlanType::INDEX_SCAN);
}

TEST_CASE("Optimizer selects SeqScan for non-indexed column filter") {
    TransactionManager tm;
    QueryEngine engine(&tm);
    engine.SeedDemoData();

    Lexer lexer("SELECT name FROM users WHERE age > 20");
    Parser parser(lexer.Tokenize());
    const Statement statement = parser.Parse();
    const std::unique_ptr<PlanNode> plan = engine.OptimizeStatement(statement);

    CHECK_FALSE(engine.PlanUsesIndexScan(*plan));
    CHECK(engine.FindAccessPlanType(*plan) == PlanType::SEQ_SCAN);
}

TEST_CASE("INSERT and DELETE statements") {
    TransactionManager tm;
    QueryEngine engine(&tm);

    (void)engine.ExecuteSql("INSERT INTO users (id, name, age) VALUES (10, 'Alice', 25)");
    const QueryResult select =
        engine.ExecuteSql("SELECT name FROM users WHERE id = 10");
    REQUIRE(select.rows.size() == 1);
    CHECK(select.rows[0] == "Alice");

    (void)engine.ExecuteSql("DELETE FROM users WHERE id = 10");
    const QueryResult after_delete =
        engine.ExecuteSql("SELECT name FROM users WHERE id = 10");
    CHECK(after_delete.rows.empty());
}

TEST_CASE("JOIN query executes via NestedLoopJoin") {
    TransactionManager tm;
    QueryEngine engine(&tm);
    engine.GetCatalog().RegisterTable({
        "scores",
        "user_id",
        {{"user_id", ColumnType::INT, true}, {"points", ColumnType::INT, false}},
    });

    (void)engine.ExecuteSql("INSERT INTO users (id, name, age) VALUES (1, 'Kartik', 20)");
    (void)engine.ExecuteSql("INSERT INTO scores (user_id, points) VALUES (1, 100)");

    const QueryResult joined = engine.ExecuteSql(
        "SELECT name FROM users JOIN scores ON id = user_id WHERE points > 50");

    REQUIRE(joined.rows.size() == 1);
    CHECK(joined.rows[0] == "Kartik");
}

TEST_CASE("QueryEngine restart rebuilds catalog for SeqScan") {
    const std::string db_path =
        std::string("/tmp/minidb_query_restart_") +
        std::to_string(static_cast<unsigned long>(std::time(nullptr))) + ".db";
    const std::string log_path = db_path.substr(0, db_path.size() - 3) + ".log";
    std::remove(db_path.c_str());
    std::remove(log_path.c_str());

    {
        minidb::TransactionManager tm(db_path, log_path);
        minidb::QueryEngine engine(&tm);
        (void)engine.ExecuteSql("INSERT INTO users (id, name, age) VALUES (42, 'Restart', 33)");
        tm.FlushRecoveryState();
        std::remove(log_path.c_str());
    }

    minidb::TransactionManager restarted(db_path, log_path);
    minidb::QueryEngine engine(&restarted);
    const minidb::QueryResult result =
        engine.ExecuteSql("SELECT name FROM users WHERE id = 42");

    REQUIRE(result.rows.size() == 1);
    CHECK(result.rows[0] == "Restart");

    std::remove(db_path.c_str());
    std::remove(log_path.c_str());
}
