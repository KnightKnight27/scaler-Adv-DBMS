#ifndef NODE_H
#define NODE_H

#include "../database.h"
#include <string>
#include <memory>

class Node {
public:
    std::string node_id;
    bool is_primary;
    bool is_online{true};
    std::unique_ptr<Database> db;

    Node(const std::string& node_id, const std::string& db_dir, bool is_primary);
    ~Node() = default;

    int execute_update(const std::string& sql, Transaction* txn = nullptr);
    std::vector<Record> execute_query(const std::string& sql, Transaction* txn = nullptr);
};

#endif
