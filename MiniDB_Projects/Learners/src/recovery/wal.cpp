#include "wal.h"
#include <sstream>
#include <iomanip>
#include <iostream>
#include "../compat.h"

std::string WAL::to_hex(const std::string& s) const {
    std::ostringstream oss;
    for (unsigned char c : s) {
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)c;
    }
    return oss.str();
}

std::string WAL::from_hex(const std::string& s) const {
    if (s.empty()) return "";
    std::string res;
    res.reserve(s.length() / 2);
    for (size_t i = 0; i < s.length(); i += 2) {
        std::string part = s.substr(i, 2);
        char chr = (char)std::stol(part, nullptr, 16);
        res.push_back(chr);
    }
    return res;
}

WAL::WAL(const std::string& log_path) : log_path(log_path) {
    // Determine next LSN by reading existing records if file exists
    std::ifstream infile(log_path);
    if (infile.is_open()) {
        std::string line;
        while (std::getline(infile, line)) {
            if (line.empty()) continue;
            size_t comma = line.find(',');
            if (comma != std::string::npos) {
                try {
                    int lsn = std::stoi(line.substr(0, comma));
                    if (lsn >= next_lsn) {
                        next_lsn = lsn + 1;
                    }
                } catch (...) {}
            }
        }
        infile.close();
    }

    log_file.open(log_path, std::ios::out | std::ios::app);
}

WAL::~WAL() {
    if (log_file.is_open()) {
        log_file.flush();
        log_file.close();
    }
}

int WAL::log_begin(int txn_id) {
    LockGuard lck(mu);
    int lsn = next_lsn++;
    log_file << lsn << "," << txn_id << ",BEGIN,,,,,\n";
    log_file.flush();
    return lsn;
}

int WAL::log_commit(int txn_id) {
    LockGuard lck(mu);
    int lsn = next_lsn++;
    log_file << lsn << "," << txn_id << ",COMMIT,,,,,\n";
    log_file.flush();
    return lsn;
}

int WAL::log_abort(int txn_id) {
    LockGuard lck(mu);
    int lsn = next_lsn++;
    log_file << lsn << "," << txn_id << ",ABORT,,,,,\n";
    log_file.flush();
    return lsn;
}

int WAL::log_update(int txn_id, const std::string& table, int page_id, int slot_id, 
                    const std::string& before_image, const std::string& after_image) {
    LockGuard lck(mu);
    int lsn = next_lsn++;
    log_file << lsn << "," 
             << txn_id << ",UPDATE," 
             << table << "," 
             << page_id << "," 
             << slot_id << "," 
             << to_hex(before_image) << "," 
             << to_hex(after_image) << "\n";
    log_file.flush();
    return lsn;
}

std::vector<LogRecord> WAL::read_all_records() {
    LockGuard lck(mu);
    // Flush to ensure we read everything
    log_file.flush();

    std::vector<LogRecord> records;
    std::ifstream infile(log_path);
    if (!infile.is_open()) {
        return records;
    }

    std::string line;
    while (std::getline(infile, line)) {
        if (line.empty()) continue;
        std::vector<std::string> tokens;
        std::string token;
        std::istringstream tokenStream(line);
        while (std::getline(tokenStream, token, ',')) {
            tokens.push_back(token);
        }

        if (tokens.size() < 3) continue;

        LogRecord rec;
        try {
            rec.lsn = std::stoi(tokens[0]);
            rec.txn_id = std::stoi(tokens[1]);
            rec.type = tokens[2];
            
            if (rec.type == "UPDATE") {
                if (tokens.size() >= 8) {
                    rec.table_name = tokens[3];
                    rec.page_id = std::stoi(tokens[4]);
                    rec.slot_id = std::stoi(tokens[5]);
                    rec.before_image = from_hex(tokens[6]);
                    rec.after_image = from_hex(tokens[7]);
                }
            }
        } catch (...) {
            continue;
        }
        records.push_back(rec);
    }
    return records;
}

void WAL::clear() {
    LockGuard lck(mu);
    if (log_file.is_open()) {
        log_file.close();
    }
    fs_compat::remove(log_path);
    next_lsn = 1;
    log_file.open(log_path, std::ios::out | std::ios::trunc);
}
