// Recovery: write-ahead logging (WAL) and crash recovery.
//
// Every change is written to an append-only log file *before* it touches the
// data, and the log records the before-image and after-image of the value.
// Each record is one line:
//     BEGIN  <txn>
//     UPDATE <txn> <key> <oldValue> <newValue>
//     COMMIT <txn>
//
// After a crash we replay the log:
//   * Analysis - find which transactions reached COMMIT (the "winners").
//   * Redo     - re-apply every committed UPDATE (idempotent).
//   * Undo     - roll back UPDATEs of transactions that never committed,
//                restoring their before-images.
//
// To keep the demonstration simple and deterministic, recovery rebuilds a
// key -> value store (the table state) from the log. This shows the WAL
// guarantees clearly without entangling the slotted-page format.
#pragma once

#include <map>
#include <string>

namespace minidb {

class LogManager {
public:
    explicit LogManager(const std::string& path);
    void logBegin(int txn);
    void logUpdate(int txn, int key, const std::string& oldVal, const std::string& newVal);
    void logCommit(int txn);
    void flush();

private:
    std::string path_;
};

// Replays a WAL file and returns the recovered key -> value store.
class RecoveryManager {
public:
    explicit RecoveryManager(const std::string& logPath) : logPath_(logPath) {}
    std::map<int, std::string> recover(int& redone, int& undone);

private:
    std::string logPath_;
};

}  // namespace minidb
