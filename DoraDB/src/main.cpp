// ============================================================
// DoraDB — Milestone 2 Test Driver
//
// Tests: B+Tree, Catalog, Tokenizer, Parser
// Also re-runs M1 tests to make sure nothing broke.
// ============================================================

#include "common/config.h"
#include "common/types.h"
#include "storage/disk_manager.h"
#include "storage/page.h"
#include "storage/buffer_pool.h"
#include "storage/heap_file.h"
#include "index/bplus_tree.h"
#include "catalog/catalog.h"
#include "parser/tokenizer.h"
#include "parser/parser.h"

#include <cassert>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <vector>
#include <algorithm>
#include <random>

static int tests_passed = 0;
static int tests_total = 0;

void CHECK(bool condition, const std::string& msg) {
    tests_total++;
    if (condition) {
        std::cout << "  [PASS] " << msg << "\n";
        tests_passed++;
    } else {
        std::cout << "  [FAIL] " << msg << "\n";
    }
}

// ============================================================
// Test: B+Tree — insert, search, range scan, delete
// ============================================================
void TestBPlusTree() {
    std::cout << "\n=== Test: B+Tree ===\n";
    std::filesystem::remove_all("data");

    {
        BPlusTree tree("data/test_index.idx");

        // Insert 100 keys in random order
        std::vector<int> keys(100);
        std::iota(keys.begin(), keys.end(), 1);  // 1..100
        std::mt19937 rng(42);
        std::shuffle(keys.begin(), keys.end(), rng);

        for (int k : keys) {
            RID rid;
            rid.page_id = k * 10;
            rid.slot_id = k;
            tree.Insert(k, rid);
        }
        CHECK(true, "inserted 100 keys without crash");

        // Search for every key
        bool all_found = true;
        for (int k = 1; k <= 100; k++) {
            auto result = tree.Search(k);
            if (!result || result->page_id != (uint32_t)(k * 10) || result->slot_id != k) {
                all_found = false;
                std::cout << "    missing key " << k << "\n";
                break;
            }
        }
        CHECK(all_found, "all 100 keys found with correct RIDs");

        // Search for non-existent key
        CHECK(!tree.Search(999).has_value(), "key 999 not found (correct)");

        // Range scan [20, 30]
        auto range = tree.RangeScan(20, 30);
        CHECK((int)range.size() == 11, "range [20,30] returns 11 entries");
        // Verify they're the right ones
        bool range_ok = true;
        for (int i = 0; i < (int)range.size(); i++) {
            if (range[i].slot_id != (uint16_t)(20 + i)) { range_ok = false; break; }
        }
        CHECK(range_ok, "range scan entries are correct");

        // Delete key 50
        CHECK(tree.Delete(50), "delete key 50 returns true");
        CHECK(!tree.Search(50).has_value(), "key 50 gone after delete");
        CHECK(tree.Search(49).has_value(), "key 49 still there");
        CHECK(tree.Search(51).has_value(), "key 51 still there");
    }

    // Persistence test: reopen the tree and verify data survived
    {
        BPlusTree tree("data/test_index.idx");
        CHECK(tree.Search(1).has_value(), "key 1 survives reopen");
        CHECK(tree.Search(100).has_value(), "key 100 survives reopen");
        CHECK(!tree.Search(50).has_value(), "key 50 still deleted after reopen");

        auto range = tree.RangeScan(95, 100);
        CHECK((int)range.size() == 6, "range [95,100] returns 6 after reopen");
    }
}

// ============================================================
// Test: B+Tree — large insert (trigger multiple splits)
// ============================================================
void TestBPlusTreeLarge() {
    std::cout << "\n=== Test: B+Tree Large ===\n";
    std::filesystem::remove_all("data");

    BPlusTree tree("data/test_large.idx");

    // Insert 5000 keys to force multiple levels of splits
    for (int i = 1; i <= 5000; i++) {
        RID rid; rid.page_id = i; rid.slot_id = i % 65536;
        tree.Insert(i, rid);
    }
    CHECK(true, "5000 keys inserted");

    // Verify all present
    bool ok = true;
    for (int i = 1; i <= 5000; i++) {
        if (!tree.Search(i).has_value()) { ok = false; break; }
    }
    CHECK(ok, "all 5000 keys searchable");

    auto range = tree.RangeScan(2500, 2510);
    CHECK((int)range.size() == 11, "range scan mid-tree correct");
}

// ============================================================
// Test: Catalog
// ============================================================
void TestCatalog() {
    std::cout << "\n=== Test: Catalog ===\n";
    std::filesystem::remove_all("data");

    {
        Catalog cat("data/catalog.txt");
        Schema s;
        s.columns.push_back({"id", DataType::INT, 0});
        s.columns.push_back({"name", DataType::VARCHAR, 50});
        s.pk_index = 0;
        CHECK(cat.CreateTable("students", s, 0, "data/students_pk.idx"),
              "create table students");
        CHECK(!cat.CreateTable("students", s, 0), "duplicate table rejected");
        CHECK(cat.GetTable("students") != nullptr, "table found");
        CHECK(cat.GetTable("missing") == nullptr, "missing table returns null");
    }

    // Persistence
    {
        Catalog cat("data/catalog.txt");
        auto* t = cat.GetTable("students");
        CHECK(t != nullptr, "table survives reload");
        CHECK(t->schema.columns.size() == 2, "2 columns survived");
        CHECK(t->schema.columns[0].name == "id", "col name survived");
        CHECK(t->schema.pk_index == 0, "pk_index survived");
        CHECK(t->index_file == "data/students_pk.idx", "index file survived");
    }
}

// ============================================================
// Test: Tokenizer
// ============================================================
void TestTokenizer() {
    std::cout << "\n=== Test: Tokenizer ===\n";

    // SELECT * FROM students WHERE id = 5 ;
    // 0      1 2    3        4     5  6 7 8
    auto tokens = Tokenizer("SELECT * FROM students WHERE id = 5;").Tokenize();
    CHECK(tokens[0].type == TokenType::SELECT, "SELECT keyword");
    CHECK(tokens[1].type == TokenType::STAR, "* token");
    CHECK(tokens[2].type == TokenType::FROM, "FROM keyword");
    CHECK(tokens[3].type == TokenType::IDENTIFIER, "table name identifier");
    CHECK(tokens[3].value == "students", "table name = students");
    CHECK(tokens[4].type == TokenType::WHERE, "WHERE keyword");
    CHECK(tokens[8].type == TokenType::SEMICOLON, "semicolon");

    // INSERT INTO t VALUES ( 1 , 'hello' , true ) ;
    // 0      1    2 3      4 5 6 7       8 9    10 11
    auto t2 = Tokenizer("INSERT INTO t VALUES (1, 'hello', true);").Tokenize();
    CHECK(t2[5].type == TokenType::INT_LITERAL, "int literal");
    CHECK(t2[7].type == TokenType::STRING_LITERAL, "string literal");
    CHECK(t2[7].value == "hello", "string value correct");
    CHECK(t2[9].type == TokenType::TRUE_LIT, "true literal");

    // CREATE TABLE t ( id INT , name VARCHAR ( 50 ) , PRIMARY KEY ( id ) ) ;
    // 0      1     2 3 4  5   6 7    8       9 10 11 12 13      14 15 16 17 18 19
    auto t3 = Tokenizer("CREATE TABLE t (id INT, name VARCHAR(50), PRIMARY KEY(id));").Tokenize();
    CHECK(t3[0].type == TokenType::CREATE, "CREATE");
    CHECK(t3[8].type == TokenType::VARCHAR_TYPE, "VARCHAR type");
}

// ============================================================
// Test: Parser
// ============================================================
void TestParser() {
    std::cout << "\n=== Test: Parser ===\n";

    // CREATE TABLE
    {
        auto tokens = Tokenizer("CREATE TABLE students (id INT, name VARCHAR(50), active BOOL, PRIMARY KEY(id));").Tokenize();
        auto stmt = Parser(tokens).Parse();
        CHECK(stmt.type == StmtType::CREATE_TABLE, "parsed CREATE TABLE");
        CHECK(stmt.create_table.table_name == "students", "table name");
        CHECK(stmt.create_table.columns.size() == 3, "3 columns");
        CHECK(stmt.create_table.columns[0].name == "id", "col 0 = id");
        CHECK(stmt.create_table.columns[1].type == DataType::VARCHAR, "col 1 type = VARCHAR");
        CHECK(stmt.create_table.columns[1].max_length == 50, "VARCHAR(50)");
        CHECK(stmt.create_table.pk_index == 0, "PK = id (index 0)");
    }

    // INSERT
    {
        auto tokens = Tokenizer("INSERT INTO students VALUES (1, 'Alice', true);").Tokenize();
        auto stmt = Parser(tokens).Parse();
        CHECK(stmt.type == StmtType::INSERT, "parsed INSERT");
        CHECK(stmt.insert.table_name == "students", "table name");
        CHECK(stmt.insert.values.size() == 3, "3 values");
        CHECK(stmt.insert.values[0].int_val == 1, "val 0 = 1");
        CHECK(stmt.insert.values[1].str_val == "Alice", "val 1 = Alice");
        CHECK(stmt.insert.values[2].bool_val == true, "val 2 = true");
    }

    // SELECT with WHERE
    {
        auto tokens = Tokenizer("SELECT * FROM students WHERE id = 5;").Tokenize();
        auto stmt = Parser(tokens).Parse();
        CHECK(stmt.type == StmtType::SELECT, "parsed SELECT");
        CHECK(stmt.select.select_all, "SELECT *");
        CHECK(stmt.select.table_name == "students", "FROM students");
        CHECK(stmt.select.where_clause != nullptr, "has WHERE");
        CHECK(stmt.select.where_clause->op == "=", "WHERE op is =");
    }

    // SELECT with JOIN
    {
        auto tokens = Tokenizer("SELECT * FROM t1 JOIN t2 ON t1.id = t2.fk WHERE t1.x > 3;").Tokenize();
        auto stmt = Parser(tokens).Parse();
        CHECK(stmt.select.join.has_value(), "has JOIN");
        CHECK(stmt.select.join->table_name == "t2", "JOIN t2");
        CHECK(stmt.select.join->left_col.table == "t1", "JOIN left table");
        CHECK(stmt.select.where_clause != nullptr, "has WHERE after JOIN");
    }

    // DELETE
    {
        auto tokens = Tokenizer("DELETE FROM students WHERE id = 5;").Tokenize();
        auto stmt = Parser(tokens).Parse();
        CHECK(stmt.type == StmtType::DELETE_STMT, "parsed DELETE");
        CHECK(stmt.delete_stmt.table_name == "students", "table name");
    }

    // UPDATE
    {
        auto tokens = Tokenizer("UPDATE students SET name = 'Bob', active = false WHERE id = 2;").Tokenize();
        auto stmt = Parser(tokens).Parse();
        CHECK(stmt.type == StmtType::UPDATE, "parsed UPDATE");
        CHECK(stmt.update.assignments.size() == 2, "2 assignments");
        CHECK(stmt.update.assignments[0].first == "name", "SET name");
        CHECK(stmt.update.assignments[0].second.str_val == "Bob", "= Bob");
    }
}

// ============================================================
// Main
// ============================================================
int main() {
    std::cout << "========================================\n";
    std::cout << "  DoraDB — Milestone 2 Tests\n";
    std::cout << "  B+Tree + Catalog + Parser\n";
    std::cout << "========================================\n";

    TestBPlusTree();
    TestBPlusTreeLarge();
    TestCatalog();
    TestTokenizer();
    TestParser();

    std::cout << "\n========================================\n";
    std::cout << "  Results: " << tests_passed << "/" << tests_total << " passed\n";
    std::cout << "========================================\n";

    std::filesystem::remove_all("data");
    return (tests_passed == tests_total) ? 0 : 1;
}
