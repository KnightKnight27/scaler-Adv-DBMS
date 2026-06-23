#include "common/rid.h"
#include "storage/disk_manager.h"
#include "storage/heap_file.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <iostream>

using namespace minidb;
using namespace std;

void TestInsertAndRead() {
  const string path = "/tmp/minidb_test_hf1.db";
  remove(path.c_str());

  DiskManager dm(path);
  HeapFile hf(&dm);

  const char* msg = "hello heap";
  RecordId rid = hf.InsertTuple(msg, static_cast<int32_t>(strlen(msg)) + 1);
  assert(rid.IsValid());

  const char* data = nullptr;
  int32_t size = 0;
  assert(hf.GetTuple(rid, data, size));
  assert(size == static_cast<int32_t>(strlen(msg)) + 1);
  assert(strcmp(data, msg) == 0);

  assert(hf.GetNumTuples() == 1);
  remove(path.c_str());
  cout << "PASS: TestInsertAndRead" << endl;
}

void TestMultipleInserts() {
  const string path = "/tmp/minidb_test_hf2.db";
  remove(path.c_str());

  DiskManager dm(path);
  HeapFile hf(&dm);

  vector<RecordId> rids;
  for (int i = 0; i < 10; ++i) {
    char buf[32];
    snprintf(buf, sizeof(buf), "tuple_%d", i);
    rids.push_back(hf.InsertTuple(buf, static_cast<int32_t>(strlen(buf)) + 1));
  }

  assert(hf.GetNumTuples() == 10);

  for (size_t i = 0; i < rids.size(); ++i) {
    const char* data = nullptr;
    int32_t size = 0;
    assert(hf.GetTuple(rids[i], data, size));
    char expected[32];
    snprintf(expected, sizeof(expected), "tuple_%zu", i);
    assert(strcmp(data, expected) == 0);
  }

  remove(path.c_str());
  cout << "PASS: TestMultipleInserts" << endl;
}

void TestUpdateTuple() {
  const string path = "/tmp/minidb_test_hf3.db";
  remove(path.c_str());

  DiskManager dm(path);
  HeapFile hf(&dm);

  RecordId rid = hf.InsertTuple("original", 9);
  const char* newData = "updated!!!";
  assert(hf.UpdateTuple(rid, newData, 11));

  const char* data = nullptr;
  int32_t size = 0;
  assert(hf.GetTuple(rid, data, size));
  assert(strcmp(data, newData) == 0);

  remove(path.c_str());
  cout << "PASS: TestUpdateTuple" << endl;
}

void TestDeleteTuple() {
  const string path = "/tmp/minidb_test_hf4.db";
  remove(path.c_str());

  DiskManager dm(path);
  HeapFile hf(&dm);

  RecordId rid1 = hf.InsertTuple("first", 6);
  RecordId rid2 = hf.InsertTuple("second", 7);
  assert(hf.GetNumTuples() == 2);

  assert(hf.DeleteTuple(rid1));
  assert(hf.GetNumTuples() == 1);

  const char* data = nullptr;
  int32_t size = 0;
  assert(!hf.GetTuple(rid1, data, size));
  assert(hf.GetTuple(rid2, data, size));
  assert(strcmp(data, "second") == 0);

  remove(path.c_str());
  cout << "PASS: TestDeleteTuple" << endl;
}

void TestPersistence() {
  const string path = "/tmp/minidb_test_hf5.db";
  remove(path.c_str());

  RecordId rid;
  {
    DiskManager dm(path);
    HeapFile hf(&dm);
    rid = hf.InsertTuple("persistent", 11);
  }

  {
    DiskManager dm(path);
    HeapFile hf(&dm);
    const char* data = nullptr;
    int32_t size = 0;
    assert(hf.GetTuple(rid, data, size));
    assert(strcmp(data, "persistent") == 0);
  }

  remove(path.c_str());
  cout << "PASS: TestPersistence" << endl;
}

int main() {
  TestInsertAndRead();
  TestMultipleInserts();
  TestUpdateTuple();
  TestDeleteTuple();
  TestPersistence();
  cout << "ALL HEAP FILE TESTS PASSED" << endl;
  return 0;
}