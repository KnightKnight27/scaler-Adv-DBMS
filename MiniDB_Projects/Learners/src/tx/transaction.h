#ifndef TRANSACTION_H
#define TRANSACTION_H

#include <string>
#include <vector>
#include <utility>

class Database;

class Transaction {
public:
    int txn_id;
    Database* db;
    bool aborted{false};
    bool committed{false};

    Transaction(int txn_id, Database* db);
    ~Transaction() = default;

    bool acquire_shared(const std::string& table_name, std::pair<int, int> rid);
    bool acquire_exclusive(const std::string& table_name, std::pair<int, int> rid);

    void commit();
    void abort();
};

#endif
