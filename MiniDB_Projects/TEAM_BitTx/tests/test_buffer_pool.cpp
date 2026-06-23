#include "buffer/buffer_pool.h"

#include <cassert>
#include <cstdio>
#include <iostream>

using namespace minidb;
using namespace std;

int main() {
  remove("/tmp/minidb_test_bp.db");
  DiskManager dm("/tmp/minidb_test_bp.db");
  BufferPool bp(&dm);

  int32_t pid = dm.AllocatePage();
  Page* p = bp.FetchPage(pid);
  assert(p != nullptr);
  memset(p->GetData(), 'A', PAGE_SIZE);
  p->GetData()[0] = 'X';
  assert(bp.UnpinPage(pid, true));

  bp.FlushAll();

  Page* p2 = bp.FetchPage(pid);
  assert(p2 != nullptr);
  assert(p2->GetData()[0] == 'X');
  bp.UnpinPage(pid, false);

  bp.FlushAll();

  remove("/tmp/minidb_test_bp.db");
  cout << "ALL BUFFER POOL TESTS PASSED" << endl;
  return 0;
}