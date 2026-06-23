#include "transaction.h"
#include "../database.h"
#include <iostream>

Transaction::Transaction(int txn_id, Database* db) : txn_id(txn_id), db(db) {
    if (db && db->wal) {
        db->wal->log_begin(txn_id);
    }
}

bool Transaction::acquire_shared(const std::string& table_name, std::pair<int, int> rid) {
    if (aborted || committed) return false;
    if (db) {
        return db->lock_manager.acquire_shared(txn_id, table_name, rid);
    }
    return true;
}

bool Transaction::acquire_exclusive(const std::string& table_name, std::pair<int, int> rid) {
    if (aborted || committed) return false;
    if (db) {
        return db->lock_manager.acquire_exclusive(txn_id, table_name, rid);
    }
    return true;
}

void Transaction::commit() {
    if (aborted || committed) return;
    if (db) {
        if (db->wal) {
            db->wal->log_commit(txn_id);
        }
        db->lock_manager.release_locks(txn_id);
    }
    committed = true;
}

void Transaction::abort() {
    if (aborted || committed) return;
    if (db) {
        if (db->wal) {
            db->wal->log_abort(txn_id);
        }
        // Rollback updates made by this transaction
        db->rollback_transaction(txn_id);
        db->lock_manager.release_locks(txn_id);
    }
    aborted = true;
}
