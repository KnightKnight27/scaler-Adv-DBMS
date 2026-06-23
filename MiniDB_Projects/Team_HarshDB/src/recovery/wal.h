#pragma once
// ---------------------------------------------------------------------------
// wal.h - the Write-Ahead Log.
//
// The golden rule of WAL: the log record describing a change is forced to disk
// BEFORE the change is allowed to be considered durable. MiniDB follows a
// no-force/redo discipline - data pages may still be sitting dirty in the buffer
// pool at commit time, but as long as the COMMIT record is on disk we can
// reconstruct the change by replaying the log after a crash.
//
// Records are stored one per line, fields separated by the ASCII unit-separator
// (0x1f) so that text values containing spaces are still parsed unambiguously.
// Each value is tagged 'I' (int) or 'S' (text).
// ---------------------------------------------------------------------------
#include "../common.h"
#include <fstream>
#include <sstream>
#include <vector>
#include <string>

namespace minidb {

enum class LogType { BEGIN, INSERT, DELETE, COMMIT, ABORT };

struct LogRecord {
    LogType     type;
    TxId        txid = INVALID_TX;
    std::string table;
    int64_t     pk = 0;     // primary key (INSERT and DELETE)
    Row         row;        // full row (INSERT only)
};

class WAL {
public:
    explicit WAL(std::string path) : path_(std::move(path)) {
        out_.open(path_, std::ios::app);
    }

    void log_begin(TxId xid)  { write({LogType::BEGIN,  xid, "", 0, {}}); }
    void log_abort(TxId xid)  { write({LogType::ABORT,  xid, "", 0, {}}); }

    void log_insert(TxId xid, const std::string& table, int64_t pk, const Row& row) {
        write({LogType::INSERT, xid, table, pk, row});
    }
    void log_delete(TxId xid, const std::string& table, int64_t pk) {
        write({LogType::DELETE, xid, table, pk, {}});
    }

    // COMMIT is the durability point: append the record and force it to disk.
    void log_commit(TxId xid) {
        write({LogType::COMMIT, xid, "", 0, {}});
        out_.flush(); // push the committed record to the OS (our fsync stand-in)
    }

    std::vector<LogRecord> read_all() {
        std::vector<LogRecord> recs;
        std::ifstream in(path_);
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            recs.push_back(decode(line));
        }
        return recs;
    }

    const std::string& path() const { return path_; }

private:
    static constexpr char SEP = '\x1f';

    void write(const LogRecord& r) {
        out_ << encode(r) << "\n";
        out_.flush();
    }

    static std::string tag(LogType t) {
        switch (t) {
            case LogType::BEGIN: return "BEGIN";
            case LogType::INSERT: return "INSERT";
            case LogType::DELETE: return "DELETE";
            case LogType::COMMIT: return "COMMIT";
            case LogType::ABORT: return "ABORT";
        }
        return "?";
    }

    static std::string encode(const LogRecord& r) {
        std::ostringstream ss;
        ss << tag(r.type) << SEP << r.txid;
        if (r.type == LogType::INSERT) {
            ss << SEP << r.table << SEP << r.pk << SEP << r.row.size();
            for (auto& v : r.row) {
                ss << SEP;
                if (auto p = std::get_if<int64_t>(&v)) ss << 'I' << *p;
                else ss << 'S' << std::get<std::string>(v);
            }
        } else if (r.type == LogType::DELETE) {
            ss << SEP << r.table << SEP << r.pk;
        }
        return ss.str();
    }

    static LogRecord decode(const std::string& line) {
        std::vector<std::string> f = split(line);
        LogRecord r;
        const std::string& t = f[0];
        if (t == "BEGIN") r.type = LogType::BEGIN;
        else if (t == "INSERT") r.type = LogType::INSERT;
        else if (t == "DELETE") r.type = LogType::DELETE;
        else if (t == "COMMIT") r.type = LogType::COMMIT;
        else r.type = LogType::ABORT;
        r.txid = (TxId)std::stoull(f[1]);
        if (r.type == LogType::INSERT) {
            r.table = f[2];
            r.pk = std::stoll(f[3]);
            int n = std::stoi(f[4]);
            for (int i = 0; i < n; ++i) {
                const std::string& cell = f[5 + i];
                if (cell[0] == 'I') r.row.push_back((int64_t)std::stoll(cell.substr(1)));
                else r.row.push_back(cell.substr(1));
            }
        } else if (r.type == LogType::DELETE) {
            r.table = f[2];
            r.pk = std::stoll(f[3]);
        }
        return r;
    }

    static std::vector<std::string> split(const std::string& line) {
        std::vector<std::string> out;
        std::string cur;
        for (char c : line) {
            if (c == SEP) { out.push_back(cur); cur.clear(); }
            else cur.push_back(c);
        }
        out.push_back(cur);
        return out;
    }

    std::string   path_;
    std::ofstream out_;
};

} // namespace minidb
