#pragma once
#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

// Write-Ahead Log.
//
// Phase E adds transaction framing so recovery can replay ONLY committed
// transactions. The log is line-based and human-readable:
//
//   BEGIN <txn>
//   STMT <txn> <sql text>
//   COMMIT <txn>
//
// Every record is flushed to disk before it is acknowledged, so a crash never
// loses a statement the client was told succeeded. A BEGIN/STMT without a
// matching COMMIT (a crash mid-transaction) is skipped on recovery.
class LogManager {
private:
    std::ofstream log_file;
    std::mutex log_lock;

public:
    LogManager(const std::string& filename) {
        log_file.open(filename, std::ios::app);
    }

    ~LogManager() {
        if (log_file.is_open()) {
            log_file.close();
        }
    }

    // Legacy raw append (kept for the storage unit tests).
    void writeLog(const std::string& sql_statement) {
        std::lock_guard<std::mutex> lock(log_lock);
        log_file << sql_statement << "\n";
        log_file.flush();
    }

    void logBegin(uint64_t txn) { writeRecord("BEGIN " + std::to_string(txn)); }
    void logStatement(uint64_t txn, const std::string& sql) {
        writeRecord("STMT " + std::to_string(txn) + " " + sql);
    }
    void logCommit(uint64_t txn) { writeRecord("COMMIT " + std::to_string(txn)); }

private:
    void writeRecord(const std::string& line) {
        std::lock_guard<std::mutex> lock(log_lock);
        log_file << line << "\n";
        log_file.flush();  // durable before commit / network send
    }
};

// Scan a WAL file and return the SQL text of every statement belonging to a
// COMMITTED transaction, in log order. Statements of uncommitted (crashed)
// transactions are dropped. Pure function -> easy to unit-test.
inline std::vector<std::string> committedStatements(const std::string& path) {
    std::ifstream in(path);
    std::vector<std::pair<uint64_t, std::string>> stmts;
    std::unordered_set<uint64_t> committed;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("STMT ", 0) == 0) {
            std::string rest = line.substr(5);
            size_t sp = rest.find(' ');
            if (sp == std::string::npos) continue;
            uint64_t txn = std::stoull(rest.substr(0, sp));
            stmts.emplace_back(txn, rest.substr(sp + 1));
        } else if (line.rfind("COMMIT ", 0) == 0) {
            committed.insert(std::stoull(line.substr(7)));
        }
        // BEGIN lines carry no statement; ignored.
    }
    std::vector<std::string> out;
    for (auto& [txn, sql] : stmts)
        if (committed.count(txn)) out.push_back(sql);
    return out;
}
