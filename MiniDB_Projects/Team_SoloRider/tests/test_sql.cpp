// tests/test_sql.cpp — Unit tests for the MiniDB SQL lexer and parser.
//
// We use Catch2 v3 (header-only via FetchContent).  Each TEST_CASE exercises
// one aspect of the tokenizer or parser, casting the AST result to the
// expected node type and verifying fields.

#include <catch2/catch_test_macros.hpp>

#include "sql/lexer.h"
#include "sql/parser.h"
#include "sql/ast.h"

using namespace minidb;

// ═══════════════════════════════════════════════════════════════
// Lexer Tests
// ═══════════════════════════════════════════════════════════════

TEST_CASE("Lexer: SELECT * FROM students WHERE id = 1", "[lexer]") {
    Lexer lexer("SELECT * FROM students WHERE id = 1");
    auto tokens = lexer.tokenize();

    // Expected: SELECT  *  FROM  students  WHERE  id  =  1  END_OF_INPUT
    REQUIRE(tokens.size() == 9);
    CHECK(tokens[0].type == TokenType::SELECT);
    CHECK(tokens[1].type == TokenType::STAR);
    CHECK(tokens[2].type == TokenType::FROM);
    CHECK(tokens[3].type == TokenType::IDENTIFIER);
    CHECK(tokens[3].value == "students");
    CHECK(tokens[4].type == TokenType::WHERE);
    CHECK(tokens[5].type == TokenType::IDENTIFIER);
    CHECK(tokens[5].value == "id");
    CHECK(tokens[6].type == TokenType::EQ);
    CHECK(tokens[7].type == TokenType::INTEGER_LIT);
    CHECK(tokens[7].value == "1");
    CHECK(tokens[8].type == TokenType::END_OF_INPUT);
}

TEST_CASE("Lexer: strings, floats, and multi-char operators", "[lexer]") {
    Lexer lexer("INSERT INTO t VALUES ('hello', 3.14, 'it''s') WHERE x != 5 AND y <= 10 AND z >= 0");
    auto tokens = lexer.tokenize();

    // Spot-check key tokens.
    // Find the string literal 'hello'.
    bool found_hello = false;
    bool found_float = false;
    bool found_escaped = false;
    bool found_neq = false;
    bool found_lte = false;
    bool found_gte = false;

    for (const auto& tok : tokens) {
        if (tok.type == TokenType::STRING_LIT && tok.value == "hello") found_hello = true;
        if (tok.type == TokenType::FLOAT_LIT  && tok.value == "3.14")  found_float = true;
        if (tok.type == TokenType::STRING_LIT && tok.value == "it's")  found_escaped = true;
        if (tok.type == TokenType::NEQ) found_neq = true;
        if (tok.type == TokenType::LTE) found_lte = true;
        if (tok.type == TokenType::GTE) found_gte = true;
    }

    CHECK(found_hello);
    CHECK(found_float);
    CHECK(found_escaped);
    CHECK(found_neq);
    CHECK(found_lte);
    CHECK(found_gte);
}

// ═══════════════════════════════════════════════════════════════
// Parser Tests
// ═══════════════════════════════════════════════════════════════

TEST_CASE("Parser: SELECT * FROM students", "[parser]") {
    auto ast = parse_sql("SELECT * FROM students");
    REQUIRE(ast != nullptr);
    REQUIRE(ast->type == ASTNodeType::SELECT_STMT);

    auto* stmt = dynamic_cast<SelectStmt*>(ast.get());
    REQUIRE(stmt != nullptr);

    // One column: StarExpr.
    REQUIRE(stmt->columns.size() == 1);
    CHECK(stmt->columns[0]->type == ASTNodeType::STAR_EXPR);

    CHECK(stmt->from_table == "students");
    CHECK(stmt->from_alias.empty());
    CHECK(stmt->joins.empty());
    CHECK(stmt->where_clause == nullptr);
}

TEST_CASE("Parser: SELECT name, age FROM students WHERE age > 18", "[parser]") {
    auto ast = parse_sql("SELECT name, age FROM students WHERE age > 18");
    REQUIRE(ast != nullptr);

    auto* stmt = dynamic_cast<SelectStmt*>(ast.get());
    REQUIRE(stmt != nullptr);

    // Two column refs.
    REQUIRE(stmt->columns.size() == 2);
    auto* col0 = dynamic_cast<ColumnRef*>(stmt->columns[0].get());
    auto* col1 = dynamic_cast<ColumnRef*>(stmt->columns[1].get());
    REQUIRE(col0 != nullptr);
    REQUIRE(col1 != nullptr);
    CHECK(col0->column_name == "name");
    CHECK(col1->column_name == "age");

    CHECK(stmt->from_table == "students");

    // WHERE clause: age > 18
    REQUIRE(stmt->where_clause != nullptr);
    auto* where = dynamic_cast<BinaryExpr*>(stmt->where_clause.get());
    REQUIRE(where != nullptr);
    CHECK(where->op == ">");

    auto* lhs = dynamic_cast<ColumnRef*>(where->left.get());
    auto* rhs = dynamic_cast<Literal*>(where->right.get());
    REQUIRE(lhs != nullptr);
    REQUIRE(rhs != nullptr);
    CHECK(lhs->column_name == "age");
    CHECK(std::get<int>(rhs->value) == 18);
}

TEST_CASE("Parser: SELECT with JOIN", "[parser]") {
    auto ast = parse_sql(
        "SELECT s.name FROM students s JOIN courses c ON s.course_id = c.id WHERE s.age > 18"
    );
    REQUIRE(ast != nullptr);

    auto* stmt = dynamic_cast<SelectStmt*>(ast.get());
    REQUIRE(stmt != nullptr);

    // Column: s.name (qualified).
    REQUIRE(stmt->columns.size() == 1);
    auto* col = dynamic_cast<ColumnRef*>(stmt->columns[0].get());
    REQUIRE(col != nullptr);
    CHECK(col->table_name == "s");
    CHECK(col->column_name == "name");

    // FROM table with alias.
    CHECK(stmt->from_table == "students");
    CHECK(stmt->from_alias == "s");

    // One JOIN clause.
    REQUIRE(stmt->joins.size() == 1);
    CHECK(stmt->joins[0].table_name == "courses");
    CHECK(stmt->joins[0].alias == "c");

    // JOIN ON condition: s.course_id = c.id
    auto* on_expr = dynamic_cast<BinaryExpr*>(stmt->joins[0].condition.get());
    REQUIRE(on_expr != nullptr);
    CHECK(on_expr->op == "=");

    auto* on_lhs = dynamic_cast<ColumnRef*>(on_expr->left.get());
    auto* on_rhs = dynamic_cast<ColumnRef*>(on_expr->right.get());
    REQUIRE(on_lhs != nullptr);
    REQUIRE(on_rhs != nullptr);
    CHECK(on_lhs->table_name == "s");
    CHECK(on_lhs->column_name == "course_id");
    CHECK(on_rhs->table_name == "c");
    CHECK(on_rhs->column_name == "id");

    // WHERE clause.
    REQUIRE(stmt->where_clause != nullptr);
    auto* where = dynamic_cast<BinaryExpr*>(stmt->where_clause.get());
    REQUIRE(where != nullptr);
    CHECK(where->op == ">");
}

TEST_CASE("Parser: INSERT INTO students VALUES (...)", "[parser]") {
    auto ast = parse_sql("INSERT INTO students VALUES (1, 'Alice', 20.5, true)");
    REQUIRE(ast != nullptr);

    auto* stmt = dynamic_cast<InsertStmt*>(ast.get());
    REQUIRE(stmt != nullptr);

    CHECK(stmt->table_name == "students");
    CHECK(stmt->column_names.empty());  // No explicit column list.
    REQUIRE(stmt->rows.size() == 1);

    const auto& row = stmt->rows[0];
    REQUIRE(row.size() == 4);
    CHECK(std::get<int>(row[0]) == 1);
    CHECK(std::get<std::string>(row[1]) == "Alice");
    CHECK(std::get<double>(row[2]) == 20.5);
    CHECK(std::get<bool>(row[3]) == true);
}

TEST_CASE("Parser: DELETE FROM students WHERE id = 1", "[parser]") {
    auto ast = parse_sql("DELETE FROM students WHERE id = 1");
    REQUIRE(ast != nullptr);

    auto* stmt = dynamic_cast<DeleteStmt*>(ast.get());
    REQUIRE(stmt != nullptr);

    CHECK(stmt->table_name == "students");

    REQUIRE(stmt->where_clause != nullptr);
    auto* where = dynamic_cast<BinaryExpr*>(stmt->where_clause.get());
    REQUIRE(where != nullptr);
    CHECK(where->op == "=");

    auto* lhs = dynamic_cast<ColumnRef*>(where->left.get());
    auto* rhs = dynamic_cast<Literal*>(where->right.get());
    REQUIRE(lhs != nullptr);
    REQUIRE(rhs != nullptr);
    CHECK(lhs->column_name == "id");
    CHECK(std::get<int>(rhs->value) == 1);
}

TEST_CASE("Parser: CREATE TABLE with PRIMARY KEY", "[parser]") {
    auto ast = parse_sql(
        "CREATE TABLE students ("
        "  id INT,"
        "  name VARCHAR(50),"
        "  gpa FLOAT,"
        "  active BOOL,"
        "  PRIMARY KEY (id)"
        ")"
    );
    REQUIRE(ast != nullptr);

    auto* stmt = dynamic_cast<CreateTableStmt*>(ast.get());
    REQUIRE(stmt != nullptr);

    CHECK(stmt->table_name == "students");
    REQUIRE(stmt->columns.size() == 4);

    CHECK(stmt->columns[0].name == "id");
    CHECK(stmt->columns[0].type == ColumnType::INT);

    CHECK(stmt->columns[1].name == "name");
    CHECK(stmt->columns[1].type == ColumnType::VARCHAR);
    CHECK(stmt->columns[1].max_length == 50);

    CHECK(stmt->columns[2].name == "gpa");
    CHECK(stmt->columns[2].type == ColumnType::FLOAT);

    CHECK(stmt->columns[3].name == "active");
    CHECK(stmt->columns[3].type == ColumnType::BOOL);

    CHECK(stmt->primary_key == "id");
}

TEST_CASE("Parser: invalid SQL throws runtime_error", "[parser]") {
    CHECK_THROWS_AS(parse_sql("FOOBAR baz"), std::runtime_error);
    CHECK_THROWS_AS(parse_sql("SELECT"), std::runtime_error);
    CHECK_THROWS_AS(parse_sql("INSERT students"), std::runtime_error);
}
