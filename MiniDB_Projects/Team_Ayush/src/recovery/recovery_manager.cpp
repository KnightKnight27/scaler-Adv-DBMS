#include "recovery/recovery_manager.h"

#include <cstring>
#include <set>
#include "storage/disk_manager.h"

namespace minidb {

int32_t RecoveryManager::ReadCell(const std::string& data_path, int32_t idx) {
  DiskManager dm(data_path);
  if (dm.NumPages() == 0) return 0;
  char page[PAGE_SIZE];
  dm.ReadPage(0, page);
  int32_t v;
  std::memcpy(&v, page + idx * 4, 4);
  return v;
}

void RecoveryManager::WriteCell(const std::string& data_path, int32_t idx, int32_t val) {
  DiskManager dm(data_path);
  char page[PAGE_SIZE];
  if (dm.NumPages() == 0) {
    std::memset(page, 0, PAGE_SIZE);
  } else {
    dm.ReadPage(0, page);
  }
  std::memcpy(page + idx * 4, &val, 4);
  dm.WritePage(0, page);
  dm.Sync();
}

std::vector<int32_t> RecoveryManager::Recover(const std::string& data_path,
                                              const std::vector<LogRecord>& recs,
                                              int num_cells, std::ostream& out) {
  DiskManager dm(data_path);
  char page[PAGE_SIZE];
  if (dm.NumPages() == 0) std::memset(page, 0, PAGE_SIZE);
  else dm.ReadPage(0, page);

  auto cell = [&](int i) -> int32_t {
    int32_t v; std::memcpy(&v, page + i * 4, 4); return v;
  };
  auto set_cell = [&](int i, int32_t v) { std::memcpy(page + i * 4, &v, 4); };

  // ---- Analysis: which transactions committed? ----
  std::set<int32_t> committed;
  for (const LogRecord& r : recs)
    if (r.type == LogType::COMMIT) committed.insert(r.txn);
  out << "Analysis: committed txns = {";
  bool first = true;
  for (int32_t t : committed) { out << (first ? "" : ",") << "T" << t; first = false; }
  out << "}\n";

  // ---- Redo: repeat history (apply every UPDATE's after-image in order). ----
  int redos = 0;
  for (const LogRecord& r : recs) {
    if (r.type == LogType::UPDATE) {
      set_cell(r.idx, r.after);
      ++redos;
    }
  }
  out << "Redo: replayed " << redos << " update(s).\n";

  // ---- Undo: roll back losers (uncommitted) in reverse order. ----
  int undos = 0;
  for (auto it = recs.rbegin(); it != recs.rend(); ++it) {
    if (it->type == LogType::UPDATE && committed.find(it->txn) == committed.end()) {
      set_cell(it->idx, it->before);
      ++undos;
    }
  }
  out << "Undo: rolled back " << undos << " update(s) from uncommitted txns.\n";

  dm.WritePage(0, page);
  dm.Sync();

  std::vector<int32_t> result;
  for (int i = 0; i < num_cells; ++i) result.push_back(cell(i));
  return result;
}

}  // namespace minidb
