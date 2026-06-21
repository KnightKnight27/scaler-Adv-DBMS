#include "catch.hpp"

#include <unistd.h>

#include <cstdio>
#include <string>

#include "buffer/buffer_pool.h"
#include "catalog/catalog.h"
#include "catalog/table.h"
#include "storage/disk_manager.h"

using namespace walterdb;

namespace {
struct Paths {
  std::string data, cat;
};
Paths temp_paths(const char* tag) {
  std::string base = std::string("/tmp/walterdb_test_") + tag + "_" + std::to_string(::getpid());
  return {base + ".wdb", base + ".catalog"};
}
Schema users_schema() {
  return Schema({{"id", TypeId::Integer, true},
                 {"name", TypeId::Varchar, false},
                 {"age", TypeId::Integer, false}});
}
Tuple user(int id, const std::string& name, int age) {
  return Tuple({Value::make_integer(id), Value::make_varchar(name), Value::make_integer(age)});
}
}  // namespace

TEST_CASE("Catalog create + Table insert/lookup/delete", "[catalog]") {
  Paths p = temp_paths("cat_basic");
  ::remove(p.data.c_str());
  ::remove(p.cat.c_str());

  DiskManager dm(p.data);
  BufferPool bp(&dm, 64, 2);
  Catalog cat(&bp, p.cat);

  REQUIRE(cat.create_table("users", users_schema()).ok());
  REQUIRE_FALSE(cat.create_table("users", users_schema()).ok());  // duplicate name

  Table* t = cat.open_table("users");
  REQUIRE(t != nullptr);
  REQUIRE(t->has_index());

  for (int i = 1; i <= 100; ++i) {
    REQUIRE(t->insert(user(i, "user" + std::to_string(i), 20 + i)).ok());
  }
  REQUIRE(t->info()->row_count == 100);

  // Duplicate primary key is rejected.
  REQUIRE(t->insert(user(50, "dup", 0)).code() == StatusCode::AlreadyExists);

  // Primary-key point lookup (index path).
  auto rid = t->lookup_pk(Value::make_integer(42));
  REQUIRE(rid.has_value());
  Tuple got = t->get(*rid).value();
  REQUIRE(got.value(1).as_varchar() == "user42");
  REQUIRE(got.value(2).as_integer() == 62);

  // Delete via index+heap and confirm gone.
  REQUIRE(t->erase(*rid));
  REQUIRE_FALSE(t->lookup_pk(Value::make_integer(42)).has_value());
  REQUIRE(t->info()->row_count == 99);

  ::remove(p.data.c_str());
  ::remove(p.cat.c_str());
}

TEST_CASE("Catalog and table data persist across reopen", "[catalog]") {
  Paths p = temp_paths("cat_persist");
  ::remove(p.data.c_str());
  ::remove(p.cat.c_str());

  {
    DiskManager dm(p.data);
    BufferPool bp(&dm, 64, 2);
    Catalog cat(&bp, p.cat);
    REQUIRE(cat.create_table("users", users_schema()).ok());
    Table* t = cat.open_table("users");
    for (int i = 1; i <= 500; ++i) t->insert(user(i, "u" + std::to_string(i), i));
    bp.flush_all();
    dm.sync();
    // Catalog destructor persists metadata (incl. row_count) here.
  }
  {
    DiskManager dm(p.data);
    BufferPool bp(&dm, 64, 2);
    Catalog cat(&bp, p.cat);

    // Schema + stats survived.
    TableInfo* info = cat.get_table("USERS");  // case-insensitive
    REQUIRE(info != nullptr);
    REQUIRE(info->schema.num_columns() == 3);
    REQUIRE(info->row_count == 500);
    REQUIRE(info->pk_column == 0);

    // Data survived; index still resolves.
    Table* t = cat.open_table("users");
    auto rid = t->lookup_pk(Value::make_integer(321));
    REQUIRE(rid.has_value());
    REQUIRE(t->get(*rid).value().value(1).as_varchar() == "u321");

    // Full heap scan sees all rows.
    int count = 0;
    for (auto c = t->heap()->scan(); c.valid(); c.next()) ++count;
    REQUIRE(count == 500);
  }
  ::remove(p.data.c_str());
  ::remove(p.cat.c_str());
}
