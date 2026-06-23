#include "storage/disk_manager.h"
#include "storage/page.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <iostream>

using namespace minidb;
using namespace std;

void TestCreateAndOpen() {
  const string path = "/tmp/minidb_test_dm1.db";
  remove(path.c_str());

  {
    DiskManager dm(path);
    assert(dm.IsOpen());

    int32_t pid = dm.AllocatePage();
    assert(pid == 0);
    char data[PAGE_SIZE];
    memset(data, 0, PAGE_SIZE);
    dm.WritePage(pid, data);
    assert(dm.GetNumPages() == 1);
  }

  {
    DiskManager dm(path);
    assert(dm.IsOpen());
    assert(dm.GetNumPages() == 1);
  }

  remove(path.c_str());
  cout << "PASS: TestCreateAndOpen" << endl;
}

void TestReadWritePage() {
  const string path = "/tmp/minidb_test_dm2.db";
  remove(path.c_str());

  DiskManager dm(path);
  int32_t pid = dm.AllocatePage();
  char writeData[PAGE_SIZE];
  memset(writeData, 0, PAGE_SIZE);
  const char* msg = "hello minidb";
  memcpy(writeData, msg, strlen(msg));
  dm.WritePage(pid, writeData);

  char readData[PAGE_SIZE];
  dm.ReadPage(pid, readData);
  assert(memcmp(readData, msg, strlen(msg)) == 0);

  remove(path.c_str());
  cout << "PASS: TestReadWritePage" << endl;
}

void TestMultiplePages() {
  const string path = "/tmp/minidb_test_dm3.db";
  remove(path.c_str());

  DiskManager dm(path);
  vector<int32_t> pids;
  for (int i = 0; i < 5; ++i) {
    pids.push_back(dm.AllocatePage());
  }
  assert(dm.GetNumPages() == 5);
  for (size_t i = 0; i < pids.size(); ++i) {
    char data[PAGE_SIZE];
    memset(data, 0, PAGE_SIZE);
    int32_t marker = static_cast<int32_t>(i) + 100;
    memcpy(data, &marker, sizeof(marker));
    dm.WritePage(pids[i], data);
  }

  for (size_t i = 0; i < pids.size(); ++i) {
    char data[PAGE_SIZE];
    dm.ReadPage(pids[i], data);
    int32_t marker;
    memcpy(&marker, data, sizeof(marker));
    assert(marker == static_cast<int32_t>(i) + 100);
  }

  remove(path.c_str());
  cout << "PASS: TestMultiplePages" << endl;
}

void TestAllocateDeallocate() {
  const string path = "/tmp/minidb_test_dm4.db";
  remove(path.c_str());

  DiskManager dm(path);
  int32_t p1 = dm.AllocatePage();
  int32_t p2 = dm.AllocatePage();
  int32_t p3 = dm.AllocatePage();
  assert(dm.GetNumPages() == 3);

  dm.DeallocatePage(p2);
  int32_t p4 = dm.AllocatePage();
  assert(p4 == p2);

  remove(path.c_str());
  cout << "PASS: TestAllocateDeallocate" << endl;
}

int main() {
  TestCreateAndOpen();
  TestReadWritePage();
  TestMultiplePages();
  TestAllocateDeallocate();
  cout << "ALL DISK MANAGER TESTS PASSED" << endl;
  return 0;
}
