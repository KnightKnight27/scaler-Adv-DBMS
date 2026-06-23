#pragma once
#include <cstdint>
#include <cstring>
#include <string>

#include "../storage/rid.h"
#include "../txn/transaction.h"

namespace minidb {

enum class LogType : int32_t { BEGIN = 0, INSERT = 1, DELETE = 2, COMMIT = 3, ABORT = 4 };

// One WAL entry. INSERT/DELETE carry the full tuple bytes (the after-image
// for INSERT, the before-image for DELETE) so both redo and undo are just
// "apply this exact byte string at this RID" - no partial-update diffing.
//
// Wire format: [lsn:i64][txn_id:i64][type:i32][table_len:i32][table]
//              [page_id:i32][slot_num:i32][data_len:i32][data]
struct LogRecord {
  int64_t lsn = 0;
  txn_id_t txn_id = 0;
  LogType type;
  std::string table;
  RID rid;
  std::string data;

  std::string Serialize() const {
    std::string out;
    out.append(reinterpret_cast<const char *>(&lsn), sizeof(lsn));
    out.append(reinterpret_cast<const char *>(&txn_id), sizeof(txn_id));
    int32_t t = static_cast<int32_t>(type);
    out.append(reinterpret_cast<const char *>(&t), sizeof(t));
    int32_t tlen = static_cast<int32_t>(table.size());
    out.append(reinterpret_cast<const char *>(&tlen), sizeof(tlen));
    out.append(table);
    out.append(reinterpret_cast<const char *>(&rid.page_id), sizeof(rid.page_id));
    out.append(reinterpret_cast<const char *>(&rid.slot_num), sizeof(rid.slot_num));
    int32_t dlen = static_cast<int32_t>(data.size());
    out.append(reinterpret_cast<const char *>(&dlen), sizeof(dlen));
    out.append(data);
    return out;
  }

  // Reads one record starting at `in[pos]`, advances pos past it. Returns
  // false at EOF.
  static bool Deserialize(const std::string &in, size_t *pos, LogRecord *out) {
    if (*pos + sizeof(int64_t) * 2 + sizeof(int32_t) * 2 > in.size()) return false;
    size_t p = *pos;
    memcpy(&out->lsn, in.data() + p, sizeof(out->lsn)); p += sizeof(out->lsn);
    memcpy(&out->txn_id, in.data() + p, sizeof(out->txn_id)); p += sizeof(out->txn_id);
    int32_t t; memcpy(&t, in.data() + p, sizeof(t)); p += sizeof(t);
    out->type = static_cast<LogType>(t);
    int32_t tlen; memcpy(&tlen, in.data() + p, sizeof(tlen)); p += sizeof(tlen);
    if (p + tlen > in.size()) return false;
    out->table = in.substr(p, tlen); p += tlen;
    if (p + sizeof(int32_t) * 3 > in.size()) return false;
    memcpy(&out->rid.page_id, in.data() + p, sizeof(out->rid.page_id)); p += sizeof(out->rid.page_id);
    memcpy(&out->rid.slot_num, in.data() + p, sizeof(out->rid.slot_num)); p += sizeof(out->rid.slot_num);
    int32_t dlen; memcpy(&dlen, in.data() + p, sizeof(dlen)); p += sizeof(dlen);
    if (p + dlen > in.size()) return false;
    out->data = in.substr(p, dlen); p += dlen;
    *pos = p;
    return true;
  }
};

}  // namespace minidb
