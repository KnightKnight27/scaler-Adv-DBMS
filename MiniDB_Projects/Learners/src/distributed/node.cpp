#include "node.h"
#include <stdexcept>

Node::Node(const std::string& node_id, const std::string& db_dir, bool is_primary)
    : node_id(node_id), is_primary(is_primary) {
    db = std::unique_ptr<Database>(new Database(db_dir, true));
}

int Node::execute_update(const std::string& sql, Transaction* txn) {
    if (!is_online) {
        throw std::runtime_error("Node " + node_id + " is offline.");
    }
    if (!is_primary) {
        throw std::runtime_error("Write operation not allowed on read-only replica node " + node_id);
    }
    return db->execute_update(sql, txn);
}

std::vector<Record> Node::execute_query(const std::string& sql, Transaction* txn) {
    if (!is_online) {
        throw std::runtime_error("Node " + node_id + " is offline.");
    }
    return db->execute_query(sql, txn);
}
