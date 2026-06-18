#ifndef TX_MANAGER_H
#define TX_MANAGER_H

#include "common.h"
#include <optional>
#include <string>

class TransactionManager {
public:
    TxID begin();
    std::optional<std::string> read(TxID xid, const RowKey& key);
    void insert(TxID xid, const RowKey& key, const std::string& value);
    void update(TxID xid, const RowKey& key, const std::string& value);
    void remove(TxID xid, const RowKey& key);
    void commit(TxID xid);
    void abort(TxID xid);
};

#endif