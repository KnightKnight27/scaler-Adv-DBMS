#include "catalog/catalog.h"
#include "common/types.h"
#include "storage/disk_manager.h"

#include <cassert>
#include <cstdio>
#include <iostream>

using namespace minidb;
using namespace std;

int main() {
  remove("/tmp/minidb_test_cat.db");
  remove("/tmp/users.tbl");

  {
    DiskManager dm("/tmp/minidb_test_cat.db");
    CatalogManager cat(&dm);
    vector<Column> cols = {Column("id", TypeId::INTEGER), Column("name", TypeId::VARCHAR)};
    Schema schema(cols);
    assert(cat.CreateTable("users", schema));
    assert(cat.HasTable("users"));
    auto* t = cat.GetTable("users");
    assert(t != nullptr);
    assert(cat.ListTables().size() == 1);
    assert(cat.DropTable("users"));
    assert(!cat.HasTable("users"));
  }

  remove("/tmp/minidb_test_cat.db");
  remove("/tmp/users.tbl");
  cout << "ALL CATALOG TESTS PASSED" << endl;
  return 0;
}