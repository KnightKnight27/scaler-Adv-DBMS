#include "replication/replica.h"
#include <fstream>
#include <chrono>
#include <iostream>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <algorithm>

namespace minidb {

Replica::Replica(const std::string& wal_path, HeapFile* heap, BPlusTree* tree)
    : wal_path_(wal_path), heap_(heap), tree_(tree) {}

Replica::~Replica() { StopReplication(); }

void Replica::StartReplication() {
    if (running_.load()) return;
    running_ = true;
    worker_  = std::thread(&Replica::WorkerLoop, this);
}

void Replica::StopReplication() {
    running_ = false;
    if (worker_.joinable()) worker_.join();
}

void Replica::WorkerLoop() {
    while (running_.load()) {
        std::ifstream wal(wal_path_, std::ios::binary);
        if (wal.is_open()) {
            std::streamoff pos;
            { std::lock_guard<std::mutex> lk(pos_latch_); pos = applied_pos_; }
            wal.seekg(pos);

            std::unordered_set<txn_id_t> committed;
            std::unordered_map<txn_id_t, std::vector<LogRecord>> logs;

            LogRecord rec;
            std::streamoff new_pos = pos;
            while (wal.read(reinterpret_cast<char*>(&rec), sizeof(LogRecord))) {
                new_pos = wal.tellg();
                if      (rec.type == LogType::COMMIT) committed.insert(rec.txn_id);
                else if (rec.type == LogType::ABORT)  logs.erase(rec.txn_id);
                else                                   logs[rec.txn_id].push_back(rec);
            }

            // Redo committed TXNs.
            std::vector<LogRecord> redo;
            for (auto tid : committed) {
                auto it = logs.find(tid);
                if (it != logs.end()) for (auto& lr : it->second) redo.push_back(lr);
            }
            std::sort(redo.begin(), redo.end(),
                      [](const LogRecord& a, const LogRecord& b){ return a.lsn < b.lsn; });

            for (const auto& lr : redo) {
                if (lr.type == LogType::INSERT) {
                    page_id_t dummy;
                    if (!tree_->Search(lr.record_id, &dummy)) {
                        Record r{lr.record_id, lr.record_val, 0, {}};
                        page_id_t pid = heap_->InsertRecord(r);
                        if (pid != INVALID_PAGE_ID) {
                            tree_->Insert(lr.record_id, pid);
                            std::cout << "[Replica] INSERT id=" << lr.record_id
                                      << " val=" << lr.record_val << "\n";
                        }
                    }
                } else if (lr.type == LogType::DELETE) {
                    if (heap_->DeleteRecord(lr.record_id)) {
                        tree_->Delete(lr.record_id);
                        std::cout << "[Replica] DELETE id=" << lr.record_id << "\n";
                    }
                }
            }

            { std::lock_guard<std::mutex> lk(pos_latch_); applied_pos_ = new_pos; }
            wal.close();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(POLL_MS));
    }
}

} // namespace minidb
