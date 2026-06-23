#include "common/rid.h"
#include "common/types.h"
#include "index/b_plus_tree.h"

#include <cassert>
#include <cstdio>
#include <iostream>

using namespace minidb;
using namespace std;

int main() {
  remove("/tmp/minidb_test_bpt.db");
  remove("/tmp/minidb_test_bpt.db.meta");

  {
    BPlusTree tree("/tmp/minidb_test_bpt.db");
    RecordId rid1(1, 0);
    RecordId rid2{1, 1};
    RecordId rid3{1, 2};

    assert(tree.Insert(Value(10), rid1));
    assert(tree.Insert(Value(20), rid2));
    assert(tree.Insert(Value(5), rid3));
  }

  remove("/tmp/minidb_test_bpt.db");
  remove("/tmp/minidb_test_bpt.db.meta");
  cout << "ALL B+ TREE TESTS PASSED" << endl;
  return 0;
}