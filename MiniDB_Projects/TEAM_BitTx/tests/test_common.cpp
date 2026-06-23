#include "common/rid.h"
#include "common/tuple.h"
#include "common/types.h"

#include <cassert>
#include <iostream>

using namespace minidb;
using namespace std;

void TestValue() {
  Value vInt(42);
  assert(vInt.GetTypeId() == TypeId::INTEGER);
  assert(vInt.GetAsInteger() == 42);
  assert(!vInt.IsNull());
  assert(vInt.ToString() == "42");

  Value vStr("hello");
  assert(vStr.GetTypeId() == TypeId::VARCHAR);
  assert(vStr.GetAsVarchar() == "hello");

  Value vBool(true);
  assert(vBool.GetAsBoolean() == true);

  Value vNull;
  assert(vNull.IsNull());
  assert(vNull.ToString() == "NULL");

  cout << "PASS: TestValue" << endl;
}

void TestValueCompare() {
  Value a(10);
  Value b(20);
  assert(a < b);
  assert(b > a);
  assert(a == Value(10));
  assert(a != b);
  cout << "PASS: TestValueCompare" << endl;
}

void TestSchema() {
  Schema schema;
  schema.AddColumn(Column("id", TypeId::INTEGER, false, true));
  schema.AddColumn(Column("name", TypeId::VARCHAR, true, false));
  schema.AddColumn(Column("age", TypeId::BIGINT, true, false));

  assert(schema.GetColumnCount() == 3);
  assert(schema.GetColumn(0).GetName() == "id");
  assert(schema.GetColumn(0).IsPrimaryKey());
  assert(!schema.GetColumn(0).IsNullable());
  assert(schema.GetColumn(1).GetType() == TypeId::VARCHAR);

  assert(schema.GetColumnIndex("name") == 1);
  assert(schema.GetColumnIndex("missing") == -1);

  assert(schema.GetTupleLength() == sizeof(int32_t) + sizeof(int64_t));
  assert(schema.ToString() == "[id:INTEGER, name:VARCHAR, age:BIGINT]");

  cout << "PASS: TestSchema" << endl;
}

void TestRecordId() {
  RecordId rid1(5, 3);
  assert(rid1.GetPageId() == 5);
  assert(rid1.GetSlotNum() == 3);
  assert(rid1.IsValid());

  RecordId rid2;
  assert(!rid2.IsValid());

  assert(rid1 == RecordId(5, 3));
  assert(rid1 != rid2);
  assert(rid1 < RecordId(6, 0));
  assert(rid1 < RecordId(5, 4));

  assert(rid1.ToString() == "RID(5,3)");

  cout << "PASS: TestRecordId" << endl;
}

void TestTuple() {
  vector<Value> values = {Value(1), Value("alice"), Value(int64_t(30))};
  Tuple tuple(values);
  assert(tuple.GetSize() == 3);
  assert(tuple.GetValue(0).GetAsInteger() == 1);
  assert(tuple.GetValue(1).GetAsVarchar() == "alice");

  tuple.SetRid(RecordId(7, 2));
  assert(tuple.GetRid().GetPageId() == 7);

  Tuple t2({Value(1), Value("alice"), Value(int64_t(30))});
  assert(tuple == t2);

  assert(tuple.ToString() == "(1, alice, 30)");

  cout << "PASS: TestTuple" << endl;
}

int main() {
  TestValue();
  TestValueCompare();
  TestSchema();
  TestRecordId();
  TestTuple();
  cout << "ALL COMMON TESTS PASSED" << endl;
  return 0;
}
