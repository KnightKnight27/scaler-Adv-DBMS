#pragma once

#include <iosfwd>
#include <string>
#include <vector>

#include "catalog.hpp"
#include "../recovery/wal.hpp"
#include "../txn/transaction.hpp"

// session: catalog + WAL + current txn
class Database {
public:
    explicit Database(const std::string& dir);

    Catalog& catalog() { return catalog_; }
    WAL&     wal()     { return wal_; }

    // BEGIN/COMMIT/ROLLBACK
    void begin();
    void commit();
    void rollback();
    bool in_transaction() const { return current_ != 0; }

    // per-stmt bracketing: implicit txn if none open
    TxID begin_stmt();
    void end_stmt(bool ok);

    // undo bookkeeping after a write
    void track_insert(const std::string& table, int pk, RowID rid);
    void track_delete(const std::string& table, int pk, const std::string& before_image);

    // redo committed, undo losers. tables must already be CREATEd.
    void recover(std::ostream& out);

private:
    struct UndoEntry { LogType type; std::string table; int pk; RowID rid; std::string image; };

    Catalog                 catalog_;
    WAL                     wal_;
    TxID                    next_txid_ = 1;
    TxID                    current_ = 0;      // 0 = none
    bool                    implicit_ = false; // started by begin_stmt()
    std::vector<UndoEntry>  undo_;             // applied writes, for rollback

    void commit_internal();
    void rollback_internal();
};
