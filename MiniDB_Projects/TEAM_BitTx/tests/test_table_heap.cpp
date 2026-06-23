#include "catalog/table_heap.h"
#include "common/types.h"

#include <cassert>
#include <cstdio>
#include <iostream>

using namespace minidb;
using namespace std;

int main() {
  remove("/tmp/minidb_test_th.db");

  Schema schema;
  schema.AddColumn(Column("id", TypeId::INTEGER, false));
  schema.AddColumn(Column("name", TypeId::VARCHAR));
  schema.AddColumn(Column("active", TypeId::BOOLEAN));

  DiskManager dm("/tmp/minidb_test_th.db");
  TableHeap th(&dm, schema);

  Tuple t1({Value(1), Value("alice"), Value(true)});
  RecordId rid1;
  assert(th.InsertTuple(t1, &rid1));
  assert(rid1.IsValid());

  Tuple t2({Value(2), Value("bob"), Value(false)});
  RecordId rid2;
  assert(th.InsertTuple(t2, &rid2));
  assert(rid2.IsValid());
  assert(rid1 != rid2);

  Tuple got;
  assert(th.GetTuple(rid1, &got));
  assert(got.GetValue(0).GetAsInteger() == 1);
  assert(got.GetValue(1).GetAsVarchar() == "alice");
  assert(got.GetValue(2).GetAsBoolean() == true);

  assert(th.GetNumTuples() == 2);

  assert(th.DeleteTuple(rid1));
  assert(th.GetNumTuples() == 1);
  assert(!th.GetTuple(rid1, &got));

  assert(th.GetTuple(rid2, &got));
  assert(got.GetValue(1).GetAsVarchar() == "bob");

  remove("/tmp/minidb_test_th.db");
  cout << "ALL TABLE HEAP TESTS PASSED" << endl;
  return 0;
}