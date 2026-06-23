#include "recovery/recovery.h"
#include <fstream>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <iostream>

namespace minidb {

Recovery::Recovery(const std::string& wal_path, HeapFile* heap, BPlusTree* tree)
    : wal_path_(wal_path), heap_(heap), tree_(tree) {}

void Recovery::Run() {
    std::ifstream in(wal_path_, std::ios::binary);
    if (!in.is_open()) {
        std::cout << "[Recovery] No WAL found – fresh start.\n";
        return;
    }

    std::unordered_set<txn_id_t> committed;
    std::unordered_map<txn_id_t, std::vector<LogRecord>> logs;

    LogRecord rec;
    while (in.read(reinterpret_cast<char*>(&rec), sizeof(LogRecord))) {
        if      (rec.type == LogType::COMMIT) committed.insert(rec.txn_id);
        else if (rec.type == LogType::ABORT)  logs.erase(rec.txn_id);
        else                                  logs[rec.txn_id].push_back(rec);
    }

    // Build sorted redo list from committed TXNs only.
    std::vector<LogRecord> redo;
    for (auto tid : committed) {
        auto it = logs.find(tid);
        if (it != logs.end())
            for (auto& lr : it->second) redo.push_back(lr);
    }
    std::sort(redo.begin(), redo.end(),
              [](const LogRecord& a, const LogRecord& b){ return a.lsn < b.lsn; });

    int cnt = 0;
    for (const auto& lr : redo) {
        if (lr.type == LogType::INSERT) {
            page_id_t dummy;
            if (!tree_->Search(lr.record_id, &dummy)) {
                Record r{lr.record_id, lr.record_val, 0, {}};
                page_id_t pid = heap_->InsertRecord(r);
                if (pid != INVALID_PAGE_ID) { tree_->Insert(lr.record_id, pid); cnt++; }
            }
        } else if (lr.type == LogType::DELETE) {
            if (heap_->DeleteRecord(lr.record_id)) { tree_->Delete(lr.record_id); cnt++; }
        }
    }
    std::cout << "[Recovery] Redone " << cnt << " op(s) from "
              << committed.size() << " committed TXN(s).\n";
}

} // namespace minidb
