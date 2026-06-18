#pragma once
// engine.h — The Database class: integration layer for all components.
//
// Database owns:
//   • DiskManager + BufferPool  (storage layer)
//   • One Heap + BPlusTree per table  (per-table data + primary key index)
//   • LockManager  (2PL concurrency control)
//   • WAL file  (write-ahead logging for recovery)
//
// The execute() method is the single entry point:
//   1. Parse SQL → Stmt
//   2. Dispatch to the appropriate handler (do_create / do_insert / ...)
//   3. For SELECT: run the cost-based optimizer then the executor
//   4. For DML: lock rows, write WAL, modify heap+index, record undo
//   5. On COMMIT: force WAL to disk, release all locks
//   6. On ABORT: replay undo log in reverse, release all locks
//
// Recovery runs once at startup: the WAL is scanned to identify committed
// transactions, then their changes are replayed against a blank database.
#include "bplustree.h"
#include "parser.h"
#include "storage.h"
#include "transaction.h"
#include <fstream>
#include <map>
#include <memory>
#include <string>

namespace minidb {

// ── Schema and catalog ────────────────────────────────────────────────────────

struct Schema {
    std::vector<ColDef> cols;
    int pk = 0;   // index of the PRIMARY KEY column in cols[]

    // Find a column by name; returns -1 if not found.
    int col_idx(const std::string& name) const;
};

struct Table {
    std::string           name;
    Schema                schema;
    std::unique_ptr<Heap> heap;
    std::unique_ptr<BPlusTree> index;
    int                   row_count = 0;  // approximate, for optimizer
};

// ── Query result ──────────────────────────────────────────────────────────────

struct Result {
    bool   ok  = true;
    std::string msg;

    // Populated for SELECT queries.
    bool                            is_query = false;
    std::vector<std::string>        col_names;
    std::vector<std::vector<Value>> rows;

    std::string plan_desc;   // set when EXPLAIN is used
};

// ── Database ──────────────────────────────────────────────────────────────────

class Database {
public:
    // Opens (or creates) a database at `path`.
    // If a WAL file exists, crash recovery runs automatically.
    explicit Database(const std::string& path);
    ~Database();

    Result execute(const std::string& sql);

    // Statistics exposed for demos and tests.
    struct RecoveryStats { int tables = 0; int rows_redone = 0; };
    RecoveryStats recovery_stats() const { return rec_; }

private:
    // ── Statement handlers ──────────────────────────────────────────────────
    Result do_create(const Stmt& s);
    Result do_insert(const Stmt& s);
    Result do_select(const Stmt& s);
    Result do_delete(const Stmt& s);
    Result do_txn   (const Stmt& s);

    // ── Row encoding ────────────────────────────────────────────────────────
    // Rows are stored as pipe-separated strings: "val1|val2|val3".
    // Pipe characters in VARCHAR values are not escaped — this is a known
    // limitation documented in the README.
    std::string encode(const Schema& sc, const std::vector<Value>& vals);
    std::vector<Value> decode(const Schema& sc, const std::string& row);

    // ── Predicate evaluation ────────────────────────────────────────────────
    bool row_matches(const std::vector<Value>& row, const Schema& sc,
                     const std::vector<Cond>& conds,
                     const std::string& qualifier = "") const;

    // ── Optimizer ───────────────────────────────────────────────────────────
    // Decides whether to use the primary-key index or a sequential scan.
    // Returns a human-readable plan description (used by EXPLAIN).
    enum class ScanType { SEQ, INDEX_POINT, INDEX_RANGE };
    struct Plan {
        ScanType    scan;
        std::string description;
        // For index scans:
        Value key_lo, key_hi;
        bool  is_range = false;
    };
    Plan optimize(const Table& t, const std::vector<Cond>& conds);

    // ── Executor helpers ────────────────────────────────────────────────────
    // Execute a plan and return matching rows + their RIDs.
    std::vector<std::pair<RID, std::vector<Value>>>
    exec_scan(Table& t, const Plan& plan, const std::vector<Cond>& conds);

    // Project a row down to only the requested columns.
    std::vector<Value> project(const std::vector<Value>& row, const Schema& sc,
                               const std::vector<std::string>& cols, bool star) const;

    // Column names after projection (for the result header).
    std::vector<std::string> project_names(const Schema& sc,
                                           const std::vector<std::string>& cols,
                                           bool star, const std::string& qualifier = "") const;

    // ── WAL helpers ─────────────────────────────────────────────────────────
    void wal_write(const std::string& line);  // append one log record and flush
    void recover();                           // called once from the constructor

    // ── Rollback ────────────────────────────────────────────────────────────
    void rollback(Txn* t);

    // ── Internal helpers ────────────────────────────────────────────────────
    Table& get_table(const std::string& name);  // throws if not found
    std::string lock_key(const std::string& table, const Value& pk);

    // ── Data members ────────────────────────────────────────────────────────
    std::string          db_path_;
    DiskManager          disk_;
    BufferPool           pool_;
    std::map<std::string, Table> tables_;
    LockManager          locks_;
    std::ofstream        wal_out_;
    std::unique_ptr<Txn> cur_txn_;      // active explicit transaction; null = auto-commit
    long                 next_txn_id_ = 1;
    RecoveryStats        rec_;
};

// Pretty-print a Result to stdout (used by the REPL and tests).
void print_result(const Result& r);

} // namespace minidb
