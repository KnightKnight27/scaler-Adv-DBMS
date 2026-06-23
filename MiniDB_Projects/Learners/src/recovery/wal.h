#ifndef WAL_H
#define WAL_H

#include <string>
#include <vector>
#include <fstream>
#include "../compat.h"

struct LogRecord {
    int lsn;
    int txn_id;
    std::string type; // "BEGIN", "COMMIT", "ABORT", "UPDATE"
    std::string table_name;
    int page_id{-1};
    int slot_id{-1};
    std::string before_image;
    std::string after_image;
};

class WAL {
private:
    std::string log_path;
    std::ofstream log_file;
    int next_lsn{1};
    Mutex mu;

    std::string to_hex(const std::string& s) const;
    std::string from_hex(const std::string& s) const;

public:
    explicit WAL(const std::string& log_path);
    ~WAL();

    int log_begin(int txn_id);
    int log_commit(int txn_id);
    int log_abort(int txn_id);
    int log_update(int txn_id, const std::string& table, int page_id, int slot_id, 
                   const std::string& before_image, const std::string& after_image);

    std::vector<LogRecord> read_all_records();
    void clear();
};

#endif
