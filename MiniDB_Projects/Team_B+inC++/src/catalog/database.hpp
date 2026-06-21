#pragma once

#include <iosfwd>
#include <string>
#include <vector>

#include "catalog.hpp"
#include "../recovery/wal.hpp"
#include "../txn/transaction.hpp"

// A database session: the catalog plus the write-ahead log plus the current
// transaction. Reads/DDL go straight to the catalog; writes (INSERT/DELETE) are
// logged here and grouped into transactions so they can be committed atomically
// or rolled back, and recovered after a crash.
//
// The REPL is single-connection, so transactions provide atomicity + durability
// (via the WAL) rather than isolation between concurrent SQL sessions — that's
// what the standalone MVCC/2PL engine and its benchmark demonstrate.
class Database {
public:
    explicit Database(const std::string& dir);

    Catalog& catalog() { return catalog_; }
    WAL&     wal()     { return wal_; }

    // Explicit transaction control (the BEGIN/COMMIT/ROLLBACK statements).
    void begin();
    void commit();
    void rollback();
    bool in_transaction() const { return current_ != 0; }

    // Per-statement transaction bracketing for INSERT/DELETE: if no explicit
    // transaction is open, begin_stmt() starts an implicit one that end_stmt()
    // auto-commits (or rolls back on failure).
    TxID begin_stmt();
    void end_stmt(bool ok);

    // Undo bookkeeping the executor calls after applying a write.
    void track_insert(const std::string& table, int pk, RowID rid);
    void track_delete(const std::string& table, int pk, const std::string& before_image);

    // Replay the WAL: REDO every committed op, UNDO every loser op. Tables must
    // already be declared (CREATE) so we know their schema/heap/index.
    void recover(std::ostream& out);

private:
    struct UndoEntry { LogType type; std::string table; int pk; RowID rid; std::string image; };

    Catalog                 catalog_;
    WAL                     wal_;
    TxID                    next_txid_ = 1;
    TxID                    current_ = 0;      // 0 = no active transaction
    bool                    implicit_ = false; // current_ was started by begin_stmt()
    std::vector<UndoEntry>  undo_;             // applied writes in this txn, for rollback

    void commit_internal();
    void rollback_internal();
};
