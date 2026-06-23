// Tests for the M2 SQL parser and the catalog (with persistence).
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>

#include "catalog/catalog.h"
#include "common/types.h"
#include "parser/lexer.h"
#include "parser/parser.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"

using namespace minidb;

static int g_failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " << #cond << "\n"; \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

static StmtPtr parse_sql(const std::string& sql) {
    Lexer lex(sql);
    Parser p(lex.tokenize());
    return p.parse();
}

static void test_parse_create() {
    auto s = parse_sql("CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR(32), score DOUBLE);");
    CHECK(s->kind() == StmtKind::CreateTable);
    auto* c = static_cast<CreateTableStmt*>(s.get());
    CHECK(c->table == "users");
    CHECK(c->columns.size() == 3);
    CHECK(c->primary_key == 0);
    CHECK(c->columns[1].type == ValueType::VARCHAR && c->columns[1].length == 32);
    CHECK(c->columns[2].type == ValueType::DOUBLE);
    std::cout << "[ok] parse CREATE TABLE\n";
}

static void test_parse_insert() {
    auto s = parse_sql("INSERT INTO users VALUES (1, 'alice', 3.5), (2, 'bob', 4.0);");
    CHECK(s->kind() == StmtKind::Insert);
    auto* ins = static_cast<InsertStmt*>(s.get());
    CHECK(ins->table == "users");
    CHECK(ins->rows.size() == 2);
    CHECK(std::get<std::int64_t>(ins->rows[0][0]) == 1);
    CHECK(std::get<std::string>(ins->rows[0][1]) == "alice");
    CHECK(std::get<double>(ins->rows[1][2]) == 4.0);
    std::cout << "[ok] parse INSERT (multi-row)\n";
}

static void test_parse_select() {
    auto s = parse_sql(
        "SELECT u.name, COUNT(*) FROM users u JOIN orders o ON u.id = o.uid "
        "WHERE u.score >= 3 AND (o.amount > 100 OR o.amount < 10) GROUP BY u.name;");
    CHECK(s->kind() == StmtKind::Select);
    auto* sel = static_cast<SelectStmt*>(s.get());
    CHECK(sel->columns.size() == 1 && sel->columns[0] == "u.name");
    CHECK(sel->aggregates.size() == 1 && sel->aggregates[0].func == "COUNT" &&
          sel->aggregates[0].column == "*");
    CHECK(sel->from_table == "users" && sel->from_alias == "u");
    CHECK(sel->join_table == "orders" && sel->join_alias == "o");
    CHECK(sel->join_on != nullptr);
    CHECK(sel->where != nullptr && sel->where->kind() == ExprKind::Binary);
    CHECK(static_cast<BinaryExpr*>(sel->where.get())->op == "AND");
    CHECK(sel->group_by.size() == 1 && sel->group_by[0] == "u.name");
    std::cout << "[ok] parse SELECT (join, where AND/OR, aggregate, group by)\n";
}

static void test_parse_delete() {
    auto s = parse_sql("DELETE FROM users WHERE id = 5;");
    CHECK(s->kind() == StmtKind::Delete);
    auto* d = static_cast<DeleteStmt*>(s.get());
    CHECK(d->table == "users" && d->where != nullptr);
    std::cout << "[ok] parse DELETE\n";
}

static void test_catalog_persistence() {
    const std::string db = "test_cat.db", meta = "test_cat.cat";
    std::remove(db.c_str());
    std::remove(meta.c_str());

    Schema schema({{"id", ValueType::INT, 8}, {"name", ValueType::VARCHAR, 32}});
    PageId heap_first, index_root;
    {
        DiskManager dm(db);
        BufferPool pool(16, &dm);
        Catalog cat(&pool, meta);
        TableInfo* t = cat.create_table("users", schema, /*pk_col=*/0);
        heap_first = t->heap_first;
        CHECK(t->primary_index() != nullptr);
        index_root = t->primary_index()->root;
        pool.flush_all();
    }
    {
        // Reopen: the catalog reloads the table from the sidecar file.
        DiskManager dm(db);
        BufferPool pool(16, &dm);
        Catalog cat(&pool, meta);
        CHECK(cat.has_table("users"));
        TableInfo* t = cat.get_table("users");
        CHECK(t->schema.num_columns() == 2);
        CHECK(t->schema.column(1).name == "name" && t->schema.column(1).length == 32);
        CHECK(t->heap_first == heap_first);
        CHECK(t->primary_index() && t->primary_index()->root == index_root);
    }
    std::remove(db.c_str());
    std::remove(meta.c_str());
    std::cout << "[ok] catalog create + persistence across reopen\n";
}

int main() {
    test_parse_create();
    test_parse_insert();
    test_parse_select();
    test_parse_delete();
    test_catalog_persistence();
    if (g_failures == 0) {
        std::cout << "ALL PARSER/CATALOG TESTS PASSED\n";
        return 0;
    }
    std::cerr << g_failures << " CHECK(s) FAILED\n";
    return 1;
}
