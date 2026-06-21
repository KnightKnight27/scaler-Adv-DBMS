#pragma once
#include "value.h"
#include "storage.h"
#include "bplustree.h"
#include "parser.h"
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <functional>

// ─── Schema types ─────────────────────────────────────────────────────────────

struct ColumnDef {
    std::string name;
    Type        type;
    bool        primary_key = false;
};

struct TableSchema {
    std::string             table_name;
    std::vector<ColumnDef>  columns;
    int                     pk_col = -1; // index of primary key column (-1 = none)

    int col_index(const std::string& name) const {
        for (int i = 0; i < (int)columns.size(); ++i)
            if (columns[i].name == name) return i;
        return -1;
    }
    std::vector<Type> types() const {
        std::vector<Type> ts;
        for (auto& c : columns) ts.push_back(c.type);
        return ts;
    }
};

using Row = std::vector<Value>;

// Query result: column names + rows
struct ResultSet {
    std::vector<std::string> columns;
    std::vector<Row>         rows;
};

// ─── Catalog ──────────────────────────────────────────────────────────────────
// Keeps track of table schemas. In a full DB this would be persisted to a
// system catalog table; here we keep it in memory and serialise to a file.
class Catalog {
public:
    void add_table(const TableSchema& schema);
    void drop_table(const std::string& name);
    const TableSchema& get(const std::string& name) const;
    bool exists(const std::string& name) const;
    std::vector<std::string> table_names() const;

    void save(const std::string& path) const;
    void load(const std::string& path);

private:
    std::map<std::string, TableSchema> tables_;
};

// ─── HeapTable ────────────────────────────────────────────────────────────────
// Row store: rows live in slotted pages on disk, accessed through the buffer
// pool. A B+ Tree index is maintained in memory for the primary key column.
class HeapTable {
public:
    HeapTable(const TableSchema& schema, const std::string& db_dir);
    ~HeapTable() { if (bp_) bp_->flush_all(); }

    // Insert a row; throws on duplicate PK
    RID insert_row(const Row& row);

    // Delete row by RID; also removes from index
    bool delete_row(RID rid, const Row& row);

    // Full heap scan: calls visitor(rid, row) for every non-deleted row
    void scan(std::function<void(RID, const Row&)> visitor) const;

    // Point lookup via primary key index (fast path)
    std::optional<Row> lookup_by_pk(const Value& pk_val) const;

    // Range scan via index
    std::vector<std::pair<RID, Row>> index_range(const Value& lo, const Value& hi) const;

    const TableSchema& schema() const { return schema_; }
    BPlusTree&         index()        { return index_; }
    const BPlusTree&   index() const  { return index_; }

    // Estimated number of live rows — used by the optimizer for join ordering.
    // Uses the primary-key index size when available (O(n) walk, cheap at our
    // scale); falls back to a page-based estimate otherwise.
    size_t approx_row_count() const;

private:
    TableSchema                 schema_;
    std::string                 db_dir_;
    std::unique_ptr<DiskManager> dm_;
    std::unique_ptr<BufferPool>  bp_;
    mutable BPlusTree            index_;   // primary key index (in-memory)

    // Ensure there is a page with free space; returns its page id
    PageId get_or_alloc_write_page();
    Row    read_row(RID rid) const;
};

// ─── Optimizer ────────────────────────────────────────────────────────────────
// Chooses between table scan and index scan based on a simple cost model.
// Cost is estimated in "pages read"; lower cost → preferred plan.
enum class ScanType { TABLE_SCAN, INDEX_POINT, INDEX_RANGE };

struct QueryPlan {
    ScanType    scan;
    std::string table;
    // For INDEX_POINT / INDEX_RANGE
    Value       lo, hi;
    bool        has_hi = false;
};

// Result of join-order selection: which input is the OUTER (streamed) relation
// of the nested-loop join, and the estimated cardinalities behind the decision.
struct JoinPlan {
    bool   from_is_outer;          // true → stream FROM, materialise JOIN table
    size_t from_rows;              // estimated rows of FROM table (after WHERE)
    size_t join_rows;              // estimated rows of JOIN table
};

class Optimizer {
public:
    // Decide how to execute the WHERE clause for the given table
    QueryPlan plan(const TableSchema& schema, const HeapTable& table,
                   bool has_where, const Condition& cond);

    // Estimated rows matching `cond` on this table (base rows × selectivity).
    size_t estimate_cardinality(const TableSchema& schema, const HeapTable& table,
                                bool has_where, const Condition& cond) const;

    // Join-order selection: materialise the SMALLER relation as the inner side
    // of the nested-loop join and stream the larger one. For a nested-loop join
    // this minimises the materialised set (memory) and the number of outer
    // iterations that each re-scan the inner.
    JoinPlan plan_join(const TableSchema& from_s, const HeapTable& from_t, bool from_has_where,
                       const Condition& where,
                       const TableSchema& join_s, const HeapTable& join_t);
private:
    // Rough selectivity: fraction of rows expected to match this condition
    double selectivity(const Condition& cond, const TableSchema& schema) const;
};

// ─── Executor ─────────────────────────────────────────────────────────────────
// Evaluates SQL statements against the catalog + heap tables.
class Executor {
public:
    explicit Executor(Catalog& catalog,
                      std::map<std::string, std::unique_ptr<HeapTable>>& tables);

    ResultSet execute_select(const SelectStmt& s);
    // Returns the RID assigned to the inserted row (needed for undo logging).
    RID       execute_insert(const InsertStmt& s);
    // Returns the (rid, before-image) of every row actually deleted.
    std::vector<std::pair<RID, Row>> execute_delete(const DeleteStmt& s);
    void      execute_create(const CreateStmt& s, const std::string& db_dir);
    void      execute_drop(const DropStmt& s);

private:
    Catalog&                                         catalog_;
    std::map<std::string, std::unique_ptr<HeapTable>>& tables_;
    Optimizer                                        opt_;

    // Evaluate a condition against a row from `schema`
    bool eval_cond(const Condition& c, const TableSchema& schema, const Row& row,
                   // optional second row for JOIN conditions
                   const TableSchema* schema2 = nullptr,
                   const Row* row2 = nullptr) const;

    Value get_value_from_row(const std::string& col, const std::string& table_hint,
                             const TableSchema& schema, const Row& row) const;
};

// ─── Database ─────────────────────────────────────────────────────────────────
// Top-level object that wires everything together.
class Database {
public:
    explicit Database(const std::string& db_dir);

    ResultSet execute(const std::string& sql);

    // Called by the transaction manager to execute with a specific txn context
    // (used for MVCC / 2PL wrapping in transaction.cpp)
    Executor& executor() { return *executor_; }
    Catalog&  catalog()  { return catalog_;   }
    const std::string& db_dir() const { return db_dir_; }
    std::map<std::string, std::unique_ptr<HeapTable>>& tables() { return tables_; }

private:
    std::string    db_dir_;
    Catalog        catalog_;
    std::map<std::string, std::unique_ptr<HeapTable>> tables_;
    std::unique_ptr<Executor> executor_;
};
