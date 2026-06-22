#include "txn/txn_manager.h"

#include "catalog/table.h"
#include "catalog/tuple.h"

namespace axiomdb {

std::unique_ptr<Transaction> TxnManager::begin() {
  txn_id_t id = next_id_.fetch_add(1);
  auto txn = std::make_unique<Transaction>(id);
  wal_->log_begin(id);
  return txn;
}

Status TxnManager::commit(Transaction* txn) {
  txn->set_state(TxnState::Shrinking);
  wal_->log_commit(txn->id());
  wal_->sync();  // a committed transaction is durable here, even before its
                 // data pages are flushed (NO-FORCE)
  lock_mgr_->unlock_all(txn);
  txn->set_state(TxnState::Committed);
  return {};
}

void TxnManager::abort(Transaction* txn) {
  txn->set_state(TxnState::Shrinking);

  // Roll back the data changes by replaying the undo log in reverse.
  auto& undo = txn->undo_log();
  for (auto it = undo.rbegin(); it != undo.rend(); ++it) {
    Table* t = catalog_->open_table_by_id(it->table_id);
    if (!t || t->info()->pk_column < 0) continue;
    Tuple row = Tuple::decode(t->schema(), it->row_image);
    const Value& pk = row.value(static_cast<size_t>(t->info()->pk_column));
    if (it->op == UndoAction::Insert) {
      t->delete_by_pk(pk);   // undo an insert
    } else {
      t->upsert(row);        // undo a delete
    }
  }

  wal_->log_abort(txn->id());
  lock_mgr_->unlock_all(txn);
  txn->set_state(TxnState::Aborted);
}

}  // namespace axiomdb
