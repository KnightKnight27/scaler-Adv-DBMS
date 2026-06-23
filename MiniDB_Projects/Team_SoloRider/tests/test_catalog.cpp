#include <catch2/catch_test_macros.hpp>
#include "catalog/catalog.h"
#include <filesystem>
#include <random>
#include <string>

namespace fs = std::filesystem;

/// Helper: create a unique temp directory for each test run
static fs::path make_temp_dir(const std::string& suffix) {
    // Use a random number to avoid collisions across parallel runs
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(10000, 99999);
    fs::path dir = fs::temp_directory_path() / ("minidb_test_catalog_" + suffix + "_" + std::to_string(dist(gen)));
    fs::create_directories(dir);
    return dir;
}

/// Helper: build a 3-column schema (id INT, name VARCHAR(50), score FLOAT)
static minidb::Schema make_test_schema() {
    std::vector<minidb::Column> cols;

    minidb::Column c1;
    c1.name = "id";
    c1.type = minidb::ColumnType::INT;
    c1.max_length = 4;
    cols.push_back(c1);

    minidb::Column c2;
    c2.name = "name";
    c2.type = minidb::ColumnType::VARCHAR;
    c2.max_length = 50;
    cols.push_back(c2);

    minidb::Column c3;
    c3.name = "score";
    c3.type = minidb::ColumnType::FLOAT;
    c3.max_length = 4;
    cols.push_back(c3);

    return minidb::Schema{std::move(cols)};
}

// ---------------------------------------------------------------------------
// Test: Create and Get Table
// ---------------------------------------------------------------------------
TEST_CASE("Catalog - Create and Get Table") {
    fs::path dir = make_temp_dir("create_get");
    minidb::Catalog catalog(dir.string());

    minidb::Schema schema = make_test_schema();
    REQUIRE(catalog.create_table("students", schema, 0));

    SECTION("get_table returns correct info") {
        minidb::TableInfo* info = catalog.get_table("students");
        REQUIRE(info != nullptr);
        CHECK(info->table_name == "students");
        CHECK(info->heap_file_path == dir.string() + "/students.db");
        CHECK(info->primary_key_column == 0);
        CHECK(info->has_index == false);
        CHECK(info->row_count == 0);
        REQUIRE(info->schema.column_count() == 3);
        CHECK(info->schema.columns[0].name == "id");
        CHECK(info->schema.columns[0].type == minidb::ColumnType::INT);
        CHECK(info->schema.columns[1].name == "name");
        CHECK(info->schema.columns[1].type == minidb::ColumnType::VARCHAR);
        CHECK(info->schema.columns[1].max_length == 50);
        CHECK(info->schema.columns[2].name == "score");
        CHECK(info->schema.columns[2].type == minidb::ColumnType::FLOAT);
        REQUIRE(info->distinct_counts.size() == 3);
        CHECK(info->distinct_counts[0] == 0);
        CHECK(info->distinct_counts[1] == 0);
        CHECK(info->distinct_counts[2] == 0);
    }

    SECTION("creating duplicate table returns false") {
        REQUIRE_FALSE(catalog.create_table("students", schema));
    }

    SECTION("get_table for nonexistent returns nullptr") {
        CHECK(catalog.get_table("nonexistent") == nullptr);
    }

    fs::remove_all(dir);
}

// ---------------------------------------------------------------------------
// Test: Drop Table
// ---------------------------------------------------------------------------
TEST_CASE("Catalog - Drop Table") {
    fs::path dir = make_temp_dir("drop");
    minidb::Catalog catalog(dir.string());

    minidb::Schema schema = make_test_schema();
    catalog.create_table("students", schema);

    SECTION("drop existing table") {
        REQUIRE(catalog.drop_table("students"));
        CHECK(catalog.get_table("students") == nullptr);
    }

    SECTION("drop nonexistent table returns false") {
        REQUIRE_FALSE(catalog.drop_table("ghost_table"));
    }

    fs::remove_all(dir);
}

// ---------------------------------------------------------------------------
// Test: Save and Load
// ---------------------------------------------------------------------------
TEST_CASE("Catalog - Save and Load") {
    fs::path dir = make_temp_dir("save_load");

    // Create a catalog with two tables and save
    {
        minidb::Catalog catalog(dir.string());

        minidb::Schema schema1 = make_test_schema();
        catalog.create_table("students", schema1, 0);
        catalog.update_stats("students", 42, {42, 30, 15});
        catalog.set_has_index("students", true);

        std::vector<minidb::Column> cols2;
        minidb::Column c1;
        c1.name = "course_id";
        c1.type = minidb::ColumnType::INT;
        c1.max_length = 4;
        cols2.push_back(c1);

        minidb::Column c2;
        c2.name = "title";
        c2.type = minidb::ColumnType::VARCHAR;
        c2.max_length = 100;
        cols2.push_back(c2);

        minidb::Schema schema2{std::move(cols2)};
        catalog.create_table("courses", schema2, 0);

        catalog.save_to_disk();
    }

    // Load into a fresh Catalog object
    {
        minidb::Catalog catalog(dir.string());
        catalog.load_from_disk();

        auto names = catalog.get_table_names();
        REQUIRE(names.size() == 2);

        // Verify "students"
        minidb::TableInfo* s = catalog.get_table("students");
        REQUIRE(s != nullptr);
        CHECK(s->table_name == "students");
        CHECK(s->primary_key_column == 0);
        CHECK(s->has_index == true);
        CHECK(s->row_count == 42);
        REQUIRE(s->schema.column_count() == 3);
        CHECK(s->schema.columns[0].name == "id");
        CHECK(s->schema.columns[1].name == "name");
        CHECK(s->schema.columns[2].name == "score");
        REQUIRE(s->distinct_counts.size() == 3);
        CHECK(s->distinct_counts[0] == 42);
        CHECK(s->distinct_counts[1] == 30);
        CHECK(s->distinct_counts[2] == 15);

        // Verify "courses"
        minidb::TableInfo* c = catalog.get_table("courses");
        REQUIRE(c != nullptr);
        CHECK(c->table_name == "courses");
        CHECK(c->primary_key_column == 0);
        CHECK(c->has_index == false);
        CHECK(c->row_count == 0);
        REQUIRE(c->schema.column_count() == 2);
        CHECK(c->schema.columns[0].name == "course_id");
        CHECK(c->schema.columns[0].type == minidb::ColumnType::INT);
        CHECK(c->schema.columns[1].name == "title");
        CHECK(c->schema.columns[1].type == minidb::ColumnType::VARCHAR);
        CHECK(c->schema.columns[1].max_length == 100);
    }

    fs::remove_all(dir);
}

// ---------------------------------------------------------------------------
// Test: Update Stats
// ---------------------------------------------------------------------------
TEST_CASE("Catalog - Update Stats") {
    fs::path dir = make_temp_dir("stats");
    minidb::Catalog catalog(dir.string());

    minidb::Schema schema = make_test_schema();
    catalog.create_table("students", schema);

    catalog.update_stats("students", 100, {100, 80, 50});

    minidb::TableInfo* info = catalog.get_table("students");
    REQUIRE(info != nullptr);
    CHECK(info->row_count == 100);
    REQUIRE(info->distinct_counts.size() == 3);
    CHECK(info->distinct_counts[0] == 100);
    CHECK(info->distinct_counts[1] == 80);
    CHECK(info->distinct_counts[2] == 50);

    fs::remove_all(dir);
}

// ---------------------------------------------------------------------------
// Test: Set Has Index
// ---------------------------------------------------------------------------
TEST_CASE("Catalog - Set Has Index") {
    fs::path dir = make_temp_dir("index");
    minidb::Catalog catalog(dir.string());

    minidb::Schema schema = make_test_schema();
    catalog.create_table("students", schema);

    // Initially false
    minidb::TableInfo* info = catalog.get_table("students");
    REQUIRE(info != nullptr);
    CHECK(info->has_index == false);

    // Set to true
    catalog.set_has_index("students", true);
    CHECK(info->has_index == true);

    // Set back to false
    catalog.set_has_index("students", false);
    CHECK(info->has_index == false);

    fs::remove_all(dir);
}
