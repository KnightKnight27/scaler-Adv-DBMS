// ============================================================================
// log_record.h  --  One entry in the Write-Ahead Log (WAL).
//
// THE WAL RULE: before any change is allowed to reach the data file on disk, a
// record describing that change must already be in the log on disk.  Given that
// rule, a crash can always be cleaned up by reading the log:
//   * REDO committed changes that had not yet reached the data file.
//   * UNDO uncommitted changes that had reached the data file.
//
// We log five kinds of events.  INSERT/DELETE carry enough information to both
// re-apply (redo) and reverse (undo) the change: the table, the exact RID, and
// the tuple bytes.
// ============================================================================
#pragma once

#include "common/common.h"

namespace minidb {

enum class LogType { kBegin, kInsert, kDelete, kCommit, kAbort };

struct LogRecord {
  lsn_t       lsn{INVALID_LSN};
  txn_id_t    txn{INVALID_TXN_ID};
  LogType     type{LogType::kBegin};
  string table;        // for INSERT/DELETE
  RID         rid;          // for INSERT/DELETE
  string tuple_bytes;  // for INSERT/DELETE (redo image == undo image here)

  // --- serialize to a self-delimiting binary blob (length-prefixed) ---
  string serialize() const {
    string body;
    auto putI32 = [&](int32_t v) { body.append((char *)&v, 4); };
    auto putI64 = [&](int64_t v) { body.append((char *)&v, 8); };
    auto putStr = [&](const string &s) {
      putI32((int32_t)s.size());
      body.append(s);
    };
    putI64(lsn);
    putI32(txn);
    putI32((int32_t)type);
    putStr(table);
    putI32(rid.page_id);
    putI32(rid.slot);
    putStr(tuple_bytes);

    string out;
    int32_t len = (int32_t)body.size();
    out.append((char *)&len, 4);   // length prefix lets recovery detect a torn tail
    out.append(body);
    return out;
  }

  // --- parse one record starting at data[*off]; advances *off.  Returns false
  //     if there are not enough bytes left (a partial record from a crash). ---
  static bool deserialize(const string &data, size_t *off, LogRecord *rec) {
    if (*off + 4 > data.size()) return false;
    int32_t len;
    memcpy(&len, data.data() + *off, 4);
    if (*off + 4 + (size_t)len > data.size()) return false;   // torn record
    size_t p = *off + 4;
    auto getI32 = [&](int32_t &v) { memcpy(&v, data.data() + p, 4); p += 4; };
    auto getI64 = [&](int64_t &v) { memcpy(&v, data.data() + p, 8); p += 8; };
    auto getStr = [&](string &s) {
      int32_t n; getI32(n); s.assign(data.data() + p, n); p += n;
    };
    int32_t t;
    getI64(rec->lsn);
    getI32(rec->txn);
    getI32(t); rec->type = (LogType)t;
    getStr(rec->table);
    getI32(rec->rid.page_id);
    getI32(rec->rid.slot);
    getStr(rec->tuple_bytes);
    *off = *off + 4 + len;
    return true;
  }
};

}  // namespace minidb
