#include "recovery.h"

#include <stdexcept>
#include <unordered_set>
#include <vector>

// ─── LogManager ──────────────────────────────────────────────────────────────

LogManager::LogManager(const std::string &path)
    : out_(path, std::ios::binary | std::ios::app) {
    if (!out_.is_open())
        throw std::runtime_error("LogManager: cannot open log file: " + path);
}

LogManager::~LogManager() {
    if (out_.is_open()) {
        out_.flush();
        out_.close();
    }
}

void LogManager::AppendRecord(const LogRecord &rec) {
    std::lock_guard<std::mutex> guard(mutex_);
    out_.write(reinterpret_cast<const char *>(&rec), sizeof(LogRecord));
}

void LogManager::Flush() {
    std::lock_guard<std::mutex> guard(mutex_);
    out_.flush();
}

// ─── RecoveryManager ─────────────────────────────────────────────────────────

RecoveryManager::RecoveryManager(const std::string &log_path, TableHeap *heap)
    : log_path_(log_path), heap_(heap) {}

void RecoveryManager::Redo() {
    std::ifstream in(log_path_, std::ios::binary);
    if (!in.is_open()) return;   // no WAL — nothing to redo

    // Pass 1: collect all committed transaction IDs.
    std::vector<LogRecord> records;
    std::unordered_set<int32_t> committed;

    LogRecord rec;
    while (in.read(reinterpret_cast<char *>(&rec), sizeof(LogRecord))) {
        records.push_back(rec);
        if (rec.type == LogType::COMMIT)
            committed.insert(rec.txn_id);
    }

    // Pass 2: redo INSERT records belonging to committed transactions, in order.
    for (const LogRecord &r : records) {
        if (r.type == LogType::INSERT && committed.count(r.txn_id)) {
            heap_->InsertTuple({r.id, r.val1, r.val2});
        }
    }
}
