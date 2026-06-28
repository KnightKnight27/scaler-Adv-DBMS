// ============================================================================
//  wal.hpp — Write-Ahead Logging and crash recovery.
//
//  The durability contract of every real database: BEFORE a change is allowed
//  to be considered durable, a log record describing it must already be on
//  disk. That is the "write-ahead" rule. Given the log, the database can always
//  reconstruct committed state after a crash, even if the in-memory data
//  (buffer pool) is lost.
//
//  Log record types:
//      BEGIN(t)            t started
//      UPDATE(t, k, v)     t set key k to v   (redo information)
//      COMMIT(t)           t's effects are durable — a "winner"
//      ABORT(t)            t is rolled back   — a "loser"
//      CHECKPOINT          a recovery start hint
//
//  Recovery (redo-of-winners — a simplified ARIES):
//      1. ANALYSIS: scan the log, collect the set of COMMITTED transactions.
//      2. REDO: replay every UPDATE whose transaction committed.
//         UPDATEs of uncommitted/aborted transactions are simply never
//         replayed — that IS the undo, because we never force dirty data to
//         disk before commit (a no-force / no-steal buffer policy).
//
//  The log is the source of truth; the in-memory map models the volatile
//  buffer pool that a crash wipes. recover() rebuilds the map from the log.
// ============================================================================
#pragma once

#include "../common/types.hpp"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace minidb {

enum class LogType : uint8_t { Begin = 1, Update = 2, Commit = 3, Abort = 4, Checkpoint = 5 };

struct LogRecord {
    LogType     type;
    uint64_t    txn = 0;
    std::string key;
    std::string value;
};

// Appends records to a log file, flushing on demand. The flush() after a COMMIT
// is what makes the write-ahead guarantee real: the COMMIT record (and every
// UPDATE before it) is on disk before commit() returns to the caller.
class LogManager {
public:
    explicit LogManager(const std::string& path) : path_(path) {
        out_.open(path, std::ios::binary | std::ios::app);
    }

    lsn_t append(const LogRecord& r) {
        lsn_t lsn = next_lsn_++;
        write_u8((uint8_t)r.type);
        write_u64(r.txn);
        write_str(r.key);
        write_str(r.value);
        return lsn;
    }

    // Force buffered log bytes to the OS. Called at commit (WAL rule).
    void flush() { out_.flush(); }

    // Read the whole log back as records (used by recovery).
    static std::vector<LogRecord> read_all(const std::string& path) {
        std::vector<LogRecord> recs;
        std::ifstream in(path, std::ios::binary);
        if (!in) return recs;
        while (in.peek() != EOF) {
            LogRecord r;
            uint8_t ty;
            if (!in.read(reinterpret_cast<char*>(&ty), 1)) break;
            r.type = (LogType)ty;
            r.txn = read_u64(in);
            r.key = read_str(in);
            r.value = read_str(in);
            recs.push_back(std::move(r));
        }
        return recs;
    }

private:
    void write_u8(uint8_t v)  { out_.write(reinterpret_cast<char*>(&v), 1); }
    void write_u64(uint64_t v){ out_.write(reinterpret_cast<char*>(&v), 8); }
    void write_str(const std::string& s) {
        uint16_t n = (uint16_t)s.size();
        out_.write(reinterpret_cast<char*>(&n), 2);
        out_.write(s.data(), n);
    }
    static uint64_t read_u64(std::ifstream& in) { uint64_t v = 0; in.read(reinterpret_cast<char*>(&v), 8); return v; }
    static std::string read_str(std::ifstream& in) {
        uint16_t n = 0; in.read(reinterpret_cast<char*>(&n), 2);
        std::string s(n, '\0'); if (n) in.read(&s[0], n); return s;
    }

    std::string   path_;
    std::ofstream out_;
    lsn_t         next_lsn_ = 0;
};

// A tiny transactional key-value store that survives crashes via its WAL. It is
// deliberately small: its only job is to make the recovery procedure concrete
// and demonstrable (crash -> recover -> committed data intact).
class WALStore {
public:
    explicit WALStore(const std::string& log_path) : log_(log_path), log_path_(log_path) {}

    uint64_t begin() {
        uint64_t t = ++next_txn_;
        log_.append({LogType::Begin, t, "", ""});
        return t;
    }

    // Log the update first (write-ahead), then buffer it in the txn's workspace.
    // It only becomes visible to other transactions at commit.
    void put(uint64_t t, const std::string& k, const std::string& v) {
        log_.append({LogType::Update, t, k, v});
        pending_[t][k] = v;
    }

    bool get(const std::string& k, std::string* out) const {
        auto it = committed_.find(k);
        if (it == committed_.end()) return false;
        *out = it->second; return true;
    }

    // WAL rule in action: append COMMIT, FLUSH the log to disk, and only then
    // apply the buffered writes to the visible store. If we crash after the
    // flush, recovery will redo these from the log.
    void commit(uint64_t t) {
        log_.append({LogType::Commit, t, "", ""});
        log_.flush();
        for (auto& [k, v] : pending_[t]) committed_[k] = v;
        pending_.erase(t);
    }

    void abort(uint64_t t) {
        log_.append({LogType::Abort, t, "", ""});
        pending_.erase(t);     // buffered writes were never visible: nothing to undo
    }

    void checkpoint() { log_.append({LogType::Checkpoint, 0, "", ""}); log_.flush(); }

    // Simulate a crash: throw away ALL volatile state (the visible store + any
    // in-flight transactions). The log file on disk is untouched.
    void crash() { committed_.clear(); pending_.clear(); }

    // Crash recovery: redo-of-winners. Returns the number of UPDATEs replayed.
    int recover() {
        auto recs = LogManager::read_all(log_path_);
        // ANALYSIS: who committed?
        std::unordered_set<uint64_t> winners;
        for (auto& r : recs) if (r.type == LogType::Commit) winners.insert(r.txn);
        // REDO: replay updates of committed transactions, in log order.
        int redone = 0;
        for (auto& r : recs)
            if (r.type == LogType::Update && winners.count(r.txn)) {
                committed_[r.key] = r.value; ++redone;
            }
        return redone;
    }

private:
    LogManager  log_;
    std::string log_path_;
    uint64_t    next_txn_ = 0;
    std::unordered_map<std::string, std::string> committed_;               // visible store
    std::unordered_map<uint64_t, std::unordered_map<std::string, std::string>> pending_;
};

}  // namespace minidb
