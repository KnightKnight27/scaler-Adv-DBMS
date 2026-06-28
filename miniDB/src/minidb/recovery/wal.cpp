#include "minidb/recovery/wal.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <utility>

namespace minidb {
namespace {

std::string TypeName(WalRecordType type) {
  switch (type) {
    case WalRecordType::Begin:
      return "BEGIN";
    case WalRecordType::Insert:
      return "INSERT";
    case WalRecordType::Delete:
      return "DELETE";
    case WalRecordType::Commit:
      return "COMMIT";
    case WalRecordType::Abort:
      return "ABORT";
  }
  throw MiniDbError("unknown WAL record type");
}

WalRecordType ParseType(const std::string& type) {
  if (type == "BEGIN") {
    return WalRecordType::Begin;
  }
  if (type == "INSERT") {
    return WalRecordType::Insert;
  }
  if (type == "DELETE") {
    return WalRecordType::Delete;
  }
  if (type == "COMMIT") {
    return WalRecordType::Commit;
  }
  if (type == "ABORT") {
    return WalRecordType::Abort;
  }
  throw MiniDbError("invalid WAL record type: " + type);
}

std::string HexEncode(std::string_view input) {
  constexpr char kDigits[] = "0123456789ABCDEF";
  std::string out;
  out.reserve(input.size() * 2);
  for (unsigned char ch : input) {
    out.push_back(kDigits[ch >> 4]);
    out.push_back(kDigits[ch & 0x0F]);
  }
  return out;
}

int HexValue(char ch) {
  if (ch >= '0' && ch <= '9') {
    return ch - '0';
  }
  if (ch >= 'A' && ch <= 'F') {
    return ch - 'A' + 10;
  }
  if (ch >= 'a' && ch <= 'f') {
    return ch - 'a' + 10;
  }
  throw MiniDbError("invalid hex digit in WAL payload");
}

std::string HexDecode(std::string_view input) {
  if (input.size() % 2 != 0) {
    throw MiniDbError("invalid WAL hex payload length");
  }
  std::string out;
  out.reserve(input.size() / 2);
  for (std::size_t i = 0; i < input.size(); i += 2) {
    out.push_back(static_cast<char>((HexValue(input[i]) << 4) | HexValue(input[i + 1])));
  }
  return out;
}

std::vector<std::string> SplitTabs(std::string_view line) {
  std::vector<std::string> parts;
  std::string current;
  for (char ch : line) {
    if (ch == '\t') {
      parts.push_back(current);
      current.clear();
    } else {
      current.push_back(ch);
    }
  }
  parts.push_back(current);
  return parts;
}

std::string RidKey(Rid rid) { return rid.ToString(); }

}  // namespace

WalLogManager::WalLogManager(std::filesystem::path path) : path_(std::move(path)) {
  if (std::filesystem::exists(path_)) {
    for (const auto& record : ReadAll()) {
      next_lsn_ = std::max(next_lsn_, record.lsn + 1);
    }
  }
}

void WalLogManager::AppendBegin(TxnId txn_id) { Append(WalRecord{0, WalRecordType::Begin, txn_id}); }

void WalLogManager::AppendInsert(TxnId txn_id, std::string table, Rid rid, std::string row) {
  Append(WalRecord{0, WalRecordType::Insert, txn_id, std::move(table), rid, std::move(row)});
}

void WalLogManager::AppendDelete(TxnId txn_id, std::string table, Rid rid, std::string old_row) {
  Append(WalRecord{0, WalRecordType::Delete, txn_id, std::move(table), rid, std::move(old_row)});
}

void WalLogManager::AppendCommit(TxnId txn_id) { Append(WalRecord{0, WalRecordType::Commit, txn_id}); }

void WalLogManager::AppendAbort(TxnId txn_id) { Append(WalRecord{0, WalRecordType::Abort, txn_id}); }

void WalLogManager::Flush() {
  std::ofstream out(path_, std::ios::app | std::ios::binary);
  if (!out) {
    throw MiniDbError("could not flush WAL: " + path_.string());
  }
  out.flush();
}

std::vector<WalRecord> WalLogManager::ReadAll() const {
  std::vector<WalRecord> records;
  std::ifstream in(path_, std::ios::binary);
  if (!in) {
    return records;
  }

  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) {
      continue;
    }
    auto parts = SplitTabs(line);
    if (parts.size() != 7) {
      throw MiniDbError("corrupt WAL record");
    }
    WalRecord record;
    record.lsn = static_cast<std::uint64_t>(std::stoull(parts[0]));
    record.type = ParseType(parts[1]);
    record.txn_id = static_cast<TxnId>(std::stoull(parts[2]));
    record.table = HexDecode(parts[3]);
    record.rid.page_id = static_cast<PageId>(std::stoul(parts[4]));
    record.rid.slot_id = static_cast<SlotId>(std::stoul(parts[5]));
    record.row = HexDecode(parts[6]);
    records.push_back(std::move(record));
  }
  return records;
}

RecoveryResult WalLogManager::Recover() const {
  auto records = ReadAll();
  RecoveryResult result;
  std::unordered_set<TxnId> aborted_txns;

  for (const auto& record : records) {
    if (record.type == WalRecordType::Commit) {
      result.committed_txns.insert(record.txn_id);
    } else if (record.type == WalRecordType::Abort) {
      aborted_txns.insert(record.txn_id);
    }
  }

  std::unordered_map<std::string, std::unordered_map<std::string, std::pair<Rid, std::string>>> rebuilt;
  for (const auto& record : records) {
    if (record.type == WalRecordType::Begin || record.type == WalRecordType::Commit ||
        record.type == WalRecordType::Abort) {
      continue;
    }
    if (result.committed_txns.find(record.txn_id) == result.committed_txns.end()) {
      result.ignored_txns.insert(record.txn_id);
      continue;
    }
    if (aborted_txns.find(record.txn_id) != aborted_txns.end()) {
      result.ignored_txns.insert(record.txn_id);
      continue;
    }
    if (record.type == WalRecordType::Insert) {
      rebuilt[record.table][RidKey(record.rid)] = {record.rid, record.row};
    } else if (record.type == WalRecordType::Delete) {
      rebuilt[record.table].erase(RidKey(record.rid));
    }
  }

  for (auto& [table, rows_by_rid] : rebuilt) {
    auto& rows = result.rows[table];
    for (auto& [rid_key, row] : rows_by_rid) {
      (void)rid_key;
      rows.push_back(std::move(row));
    }
    std::sort(rows.begin(), rows.end(), [](const auto& lhs, const auto& rhs) {
      if (lhs.first.page_id != rhs.first.page_id) {
        return lhs.first.page_id < rhs.first.page_id;
      }
      return lhs.first.slot_id < rhs.first.slot_id;
    });
  }
  return result;
}

void WalLogManager::Append(WalRecord record) {
  record.lsn = next_lsn_++;
  std::ofstream out(path_, std::ios::app | std::ios::binary);
  if (!out) {
    throw MiniDbError("could not append WAL record: " + path_.string());
  }
  out << record.lsn << '\t' << TypeName(record.type) << '\t' << record.txn_id << '\t' << HexEncode(record.table)
      << '\t' << record.rid.page_id << '\t' << record.rid.slot_id << '\t' << HexEncode(record.row) << '\n';
  out.flush();
}

}  // namespace minidb
