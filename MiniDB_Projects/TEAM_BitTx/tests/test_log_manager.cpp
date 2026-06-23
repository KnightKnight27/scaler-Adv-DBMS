#include "recovery/log_manager.h"

#include <cassert>
#include <cstdio>
#include <iostream>

using namespace minidb;
using namespace std;

int main() {
  remove("/tmp/minidb_test_log.wal");

  {
    LogManager lm("/tmp/minidb_test_log.wal");
    LogRecord r1;
    r1.type = LogRecordType::BEGIN;
    r1.txnId = 1;
    lm.Append(r1);
    r1.type = LogRecordType::INSERT;
    r1.txnId = 1;
    r1.pageId = 7;
    r1.slotId = 3;
    r1.newData = "hello";
    lm.Append(r1);
    r1.type = LogRecordType::COMMIT;
    r1.txnId = 1;
    lm.Append(r1);
  }

  {
    LogManager lm("/tmp/minidb_test_log.wal");
    auto recs = lm.ReadAll();
    assert(recs.size() == 3);
    assert(recs[0].type == LogRecordType::BEGIN);
    assert(recs[0].txnId == 1);
    assert(recs[1].type == LogRecordType::INSERT);
    assert(recs[1].pageId == 7);
    assert(recs[1].slotId == 3);
    assert(recs[1].newData == "hello");
    assert(recs[2].type == LogRecordType::COMMIT);
    lm.Truncate();
    assert(lm.ReadAll().empty());
  }

  remove("/tmp/minidb_test_log.wal");
  cout << "ALL LOG MANAGER TESTS PASSED" << endl;
  return 0;
}