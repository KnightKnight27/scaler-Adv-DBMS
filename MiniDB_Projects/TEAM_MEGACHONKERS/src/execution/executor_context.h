#pragma once

#include "catalog/catalog.h"
#include "common/config.h"

namespace minidb {

class ExecutorContext {
private:
    Catalog* catalog_;
    txn_id_t txn_id_; 

public:
    ExecutorContext(Catalog* catalog, txn_id_t txn_id = INVALID_TXN_ID)
        : catalog_(catalog), txn_id_(txn_id) {}

    Catalog* GetCatalog() const { return catalog_; }
    txn_id_t GetTransactionId() const { return txn_id_; }
};

} // namespace minidb