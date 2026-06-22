#pragma once

#include "parser.h"
#include "optimizer.h"
#include "storage/lsm_engine.h"
#include <unordered_map>
#include <functional>

namespace minidb {

// forward declarations
class Catalog;
class TransactionManager;

// queryexecutor — executes parsed sql statements against the lsm engine

class QueryExecutor {
public:
    QueryExecutor(LSMEngine& storage, Catalog& catalog,
                  TransactionManager* txn_mgr = nullptr);

    // execute a parsed statement.  returns a result set (vector of records)
    // for select queries, empty for others, or with error message.
    struct ExecResult {
        bool                     success = false;
        std::string              error;
        std::vector<std::string> column_names;
        std::vector<Record>      rows;
        size_t                   affected_rows = 0;
    };

    ExecResult execute(const Statement& stmt, TxnID txn_id = INVALID_TXN);

private:
    ExecResult exec_create_table(const CreateTableStmt& stmt);
    ExecResult exec_insert(const InsertStmt& stmt, TxnID txn_id);
    ExecResult exec_select(const SelectStmt& stmt, TxnID txn_id);
    ExecResult exec_delete(const DeleteStmt& stmt, TxnID txn_id);

    // helper: compare a record value against a where condition
    bool evaluate_where(const Record& rec, const std::vector<std::string>& col_names,
                        const WhereClause& where);

    // helper: get column index by name
    int get_column_index(const std::vector<std::string>& col_names,
                         const std::string& name) const;

    LSMEngine&           _storage;
    Catalog&             _catalog;
    TransactionManager*  _txn_mgr;
    Optimizer            _optimizer;
};

// catalog — stores table/column metadata

struct TableMeta {
    std::string            name;
    std::vector<ColumnDef> columns;
    std::string            primary_key;
    size_t                 row_count = 0;
};

class Catalog {
public:
    bool create_table(const std::string& name,
                      const std::vector<ColumnDef>& columns,
                      const std::string& primary_key);

    bool table_exists(const std::string& name) const;

    const TableMeta* get_table(const std::string& name) const;

    std::vector<std::string> column_names(const std::string& table_name) const;

    ColumnID column_index(const std::string& table_name,
                          const std::string& col_name) const;

    ColumnID primary_key_index(const std::string& table_name) const;

    void increment_row_count(const std::string& table_name);
    void decrement_row_count(const std::string& table_name);

    const std::unordered_map<std::string, TableMeta>& tables() const {
        return _tables;
    }

private:
    std::unordered_map<std::string, TableMeta> _tables;
};

// catalog implementation

inline bool Catalog::create_table(const std::string& name,
                                   const std::vector<ColumnDef>& columns,
                                   const std::string& primary_key) {
    if (_tables.count(name)) return false;
    TableMeta meta{name, columns, primary_key, 0};
    _tables[name] = meta;
    return true;
}

inline bool Catalog::table_exists(const std::string& name) const {
    return _tables.count(name) > 0;
}

inline const TableMeta* Catalog::get_table(const std::string& name) const {
    auto it = _tables.find(name);
    return (it != _tables.end()) ? &it->second : nullptr;
}

inline std::vector<std::string> Catalog::column_names(const std::string& table_name) const {
    auto* meta = get_table(table_name);
    if (!meta) return {};
    std::vector<std::string> names;
    for (const auto& col : meta->columns) names.push_back(col.name);
    return names;
}

inline ColumnID Catalog::column_index(const std::string& table_name,
                                       const std::string& col_name) const {
    auto* meta = get_table(table_name);
    if (!meta) return static_cast<ColumnID>(-1);
    for (size_t i = 0; i < meta->columns.size(); ++i) {
        if (meta->columns[i].name == col_name) return static_cast<ColumnID>(i);
    }
    return static_cast<ColumnID>(-1);
}

inline ColumnID Catalog::primary_key_index(const std::string& table_name) const {
    auto* meta = get_table(table_name);
    if (!meta || meta->primary_key.empty()) return static_cast<ColumnID>(-1);
    return column_index(table_name, meta->primary_key);
}

inline void Catalog::increment_row_count(const std::string& table_name) {
    auto it = _tables.find(table_name);
    if (it != _tables.end()) ++it->second.row_count;
}

inline void Catalog::decrement_row_count(const std::string& table_name) {
    auto it = _tables.find(table_name);
    if (it != _tables.end() && it->second.row_count > 0)
        --it->second.row_count;
}

// queryexecutor implementation

inline QueryExecutor::QueryExecutor(LSMEngine& storage, Catalog& catalog,
                                     TransactionManager* txn_mgr)
    : _storage(storage), _catalog(catalog), _txn_mgr(txn_mgr) {}

inline QueryExecutor::ExecResult QueryExecutor::execute(const Statement& stmt,
                                                         TxnID txn_id) {
    switch (stmt.type) {
        case Statement::CREATE: return exec_create_table(stmt.create);
        case Statement::INSERT: return exec_insert(stmt.insert, txn_id);
        case Statement::SELECT: return exec_select(stmt.select, txn_id);
        case Statement::DELETE: return exec_delete(stmt.del, txn_id);
        default: return {false, "Unknown statement type"};
    }
}

inline QueryExecutor::ExecResult QueryExecutor::exec_create_table(
    const CreateTableStmt& stmt) {
    if (_catalog.table_exists(stmt.table_name)) {
        return {false, "Table '" + stmt.table_name + "' already exists"};
    }
    if (stmt.primary_key.empty()) {
        return {false, "A PRIMARY KEY column must be specified"};
    }
    _catalog.create_table(stmt.table_name, stmt.columns, stmt.primary_key);

    // initialize optimizer stats for the new table
    TableStats stats;
    stats.row_count = 0;
    _optimizer.set_table_stats(stmt.table_name, stats);

    return {true, "", {}, {}, 0};
}

inline QueryExecutor::ExecResult QueryExecutor::exec_insert(const InsertStmt& stmt,
                                                             TxnID txn_id) {
    auto* meta = _catalog.get_table(stmt.table_name);
    if (!meta) return {false, "Table '" + stmt.table_name + "' not found"};

    if (stmt.values.size() != meta->columns.size()) {
        return {false, "Column count mismatch: expected " +
                std::to_string(meta->columns.size()) + ", got " +
                std::to_string(stmt.values.size())};
    }

    // extract primary key value
    ColumnID pk_idx = _catalog.primary_key_index(stmt.table_name);
    if (pk_idx == static_cast<ColumnID>(-1)) {
        return {false, "No primary key defined"};
    }
    Key pk = stmt.values[pk_idx];

    // store in lsm engine with composite key: table_id + primary_key
    // we use a fixed table_id mapping (simple hash of table name)
    TableID tid = static_cast<TableID>(
        std::hash<std::string>{}(stmt.table_name) & 0x7FFFFFFF);

    _storage.put(tid, pk, stmt.values);
    _catalog.increment_row_count(stmt.table_name);

    // update optimizer stats
    TableStats stats;
    stats.row_count = meta->row_count;
    for (const auto& col : meta->columns) {
        // simple heuristic: assume distinct = row_count (worst case)
        stats.distinct_counts[col.name] = meta->row_count;
    }
    _optimizer.set_table_stats(stmt.table_name, stats);

    return {true, "", {}, {}, 1};
}

inline QueryExecutor::ExecResult QueryExecutor::exec_select(const SelectStmt& stmt,
                                                             TxnID /*txn_id*/) {
    auto* meta = _catalog.get_table(stmt.table_name);
    if (!meta) return {false, "Table '" + stmt.table_name + "' not found"};

    TableID tid = static_cast<TableID>(
        std::hash<std::string>{}(stmt.table_name) & 0x7FFFFFFF);

    ExecResult result;
    result.success = true;

    auto col_names = _catalog.column_names(stmt.table_name);

    // --- join path: skip single-table column resolution -----------------
    if (stmt.join.has_join) {
        auto* right_meta = _catalog.get_table(stmt.join.table_name);
        if (!right_meta) return {false, "Join table '" + stmt.join.table_name + "' not found"};

        TableID right_tid = static_cast<TableID>(
            std::hash<std::string>{}(stmt.join.table_name) & 0x7FFFFFFF);

        auto right_col_names = _catalog.column_names(stmt.join.table_name);

        ColumnID join_left_idx = _catalog.column_index(
            stmt.table_name, stmt.join.left_col);
        ColumnID join_right_idx = _catalog.column_index(
            stmt.join.table_name, stmt.join.right_col);

        if (join_left_idx == static_cast<ColumnID>(-1) ||
            join_right_idx == static_cast<ColumnID>(-1)) {
            return {false, "Join column not found"};
        }

        // build output column spec: (from_right_table, column_index) pairs
        struct OutCol { bool from_right; ColumnID idx; };
        std::vector<OutCol> out_spec;
        if (stmt.columns.empty() || (stmt.columns.size() == 1 && stmt.columns[0] == "*")) {
            for (size_t i = 0; i < meta->columns.size(); ++i) {
                out_spec.push_back({false, static_cast<ColumnID>(i)});
                result.column_names.push_back(col_names[i]);
            }
            for (size_t i = 0; i < right_meta->columns.size(); ++i) {
                out_spec.push_back({true, static_cast<ColumnID>(i)});
                result.column_names.push_back(right_col_names[i]);
            }
        } else {
            for (const auto& col : stmt.columns) {
                ColumnID idx = _catalog.column_index(stmt.table_name, col);
                if (idx != static_cast<ColumnID>(-1)) {
                    out_spec.push_back({false, idx});
                    result.column_names.push_back(col);
                } else {
                    idx = _catalog.column_index(stmt.join.table_name, col);
                    if (idx != static_cast<ColumnID>(-1)) {
                        out_spec.push_back({true, idx});
                        result.column_names.push_back(col);
                    } else {
                        return {false, "Column '" + col + "' not found"};
                    }
                }
            }
        }

        std::vector<Record> left_rows, right_rows;
        _storage.full_scan(tid, [&](const Key&, const Record& rec) {
            left_rows.push_back(rec);
        });
        _storage.full_scan(right_tid, [&](const Key&, const Record& rec) {
            right_rows.push_back(rec);
        });

        for (const auto& left : left_rows) {
            if (stmt.where.has_where &&
                !evaluate_where(left, col_names, stmt.where)) continue;

            for (const auto& right : right_rows) {
                if (join_left_idx < left.size() && join_right_idx < right.size() &&
                    left[join_left_idx] == right[join_right_idx]) {

                    Record out;
                    for (const auto& oc : out_spec) {
                        if (!oc.from_right && oc.idx < left.size())
                            out.push_back(left[oc.idx]);
                        else if (oc.from_right && oc.idx < right.size())
                            out.push_back(right[oc.idx]);
                    }
                    result.rows.push_back(out);
                }
            }
        }

        return result;
    }

    // --- single-table path: resolve columns -----------------------------
    // determine which columns to return
    std::vector<std::string> out_columns;
    std::vector<ColumnID> col_indices;
    if (stmt.columns.empty() || (stmt.columns.size() == 1 && stmt.columns[0] == "*")) {
        out_columns = _catalog.column_names(stmt.table_name);
        for (size_t i = 0; i < meta->columns.size(); ++i)
            col_indices.push_back(static_cast<ColumnID>(i));
    } else {
        out_columns = stmt.columns;
        for (const auto& col : stmt.columns) {
            ColumnID idx = _catalog.column_index(stmt.table_name, col);
            if (idx == static_cast<ColumnID>(-1)) {
                return {false, "Column '" + col + "' not found"};
            }
            col_indices.push_back(idx);
        }
    }
    result.column_names = out_columns;

    // check if we can use index scan (via where on primary key)
    bool use_index_scan = false;
    ColumnID pk_idx = _catalog.primary_key_index(stmt.table_name);
    if (stmt.where.has_where && pk_idx != static_cast<ColumnID>(-1)) {
        use_index_scan = _optimizer.prefer_index_scan(
            stmt.table_name, stmt.where.column, stmt.where);
    }

    // join execution
    if (stmt.join.has_join) {
        auto* right_meta = _catalog.get_table(stmt.join.table_name);
        if (!right_meta) return {false, "Join table '" + stmt.join.table_name + "' not found"};

        TableID right_tid = static_cast<TableID>(
            std::hash<std::string>{}(stmt.join.table_name) & 0x7FFFFFFF);

        auto right_col_names = _catalog.column_names(stmt.join.table_name);

        ColumnID join_left_idx = _catalog.column_index(
            stmt.table_name, stmt.join.left_col);
        ColumnID join_right_idx = _catalog.column_index(
            stmt.join.table_name, stmt.join.right_col);

        if (join_left_idx == static_cast<ColumnID>(-1) ||
            join_right_idx == static_cast<ColumnID>(-1)) {
            return {false, "Join column not found"};
        }

        // build output column spec: (left_table_side, column_index) pairs
        struct OutCol { bool from_right; ColumnID idx; };
        std::vector<OutCol> out_spec;
        if (stmt.columns.empty() || (stmt.columns.size() == 1 && stmt.columns[0] == "*")) {
            for (size_t i = 0; i < meta->columns.size(); ++i) {
                out_spec.push_back({false, static_cast<ColumnID>(i)});
                result.column_names.push_back(col_names[i]);
            }
            for (size_t i = 0; i < right_meta->columns.size(); ++i) {
                out_spec.push_back({true, static_cast<ColumnID>(i)});
                result.column_names.push_back(right_col_names[i]);
            }
        } else {
            for (const auto& col : stmt.columns) {
                ColumnID idx = _catalog.column_index(stmt.table_name, col);
                if (idx != static_cast<ColumnID>(-1)) {
                    out_spec.push_back({false, idx});
                    result.column_names.push_back(col);
                } else {
                    idx = _catalog.column_index(stmt.join.table_name, col);
                    if (idx != static_cast<ColumnID>(-1)) {
                        out_spec.push_back({true, idx});
                        result.column_names.push_back(col);
                    } else {
                        return {false, "Column '" + col + "' not found"};
                    }
                }
            }
        }

        std::vector<Record> left_rows, right_rows;
        _storage.full_scan(tid, [&](const Key&, const Record& rec) {
            left_rows.push_back(rec);
        });
        _storage.full_scan(right_tid, [&](const Key&, const Record& rec) {
            right_rows.push_back(rec);
        });

        for (const auto& left : left_rows) {
            if (stmt.where.has_where &&
                !evaluate_where(left, col_names, stmt.where)) continue;

            for (const auto& right : right_rows) {
                if (join_left_idx < left.size() && join_right_idx < right.size() &&
                    left[join_left_idx] == right[join_right_idx]) {

                    Record out;
                    for (const auto& oc : out_spec) {
                        if (!oc.from_right && oc.idx < left.size())
                            out.push_back(left[oc.idx]);
                        else if (oc.from_right && oc.idx < right.size())
                            out.push_back(right[oc.idx]);
                    }
                    result.rows.push_back(out);
                }
            }
        }

        return result;
    }

    // simple select (no join)
    // decide scan strategy
    if (use_index_scan && stmt.where.has_where &&
        stmt.where.column == meta->primary_key && stmt.where.op == "=") {
        // point lookup via lsm — fastest path
        Key pk_val = stmt.where.value;
        bool found = false;
        Record rec = _storage.get(tid, pk_val, found);
        if (found && evaluate_where(rec, col_names, stmt.where)) {
            Record out;
            for (auto ci : col_indices) {
                if (ci < rec.size()) out.push_back(rec[ci]);
                else out.push_back(Value());
            }
            result.rows.push_back(out);
        }
    } else {
        // full table scan
        _storage.full_scan(tid, [&](const Key&, const Record& rec) {
            if (evaluate_where(rec, col_names, stmt.where)) {
                Record out;
                for (auto ci : col_indices) {
                    if (ci < rec.size()) out.push_back(rec[ci]);
                    else out.push_back(Value());
                }
                result.rows.push_back(out);
            }
        });
    }

    return result;
}

inline QueryExecutor::ExecResult QueryExecutor::exec_delete(const DeleteStmt& stmt,
                                                              TxnID /*txn_id*/) {
    auto* meta = _catalog.get_table(stmt.table_name);
    if (!meta) return {false, "Table '" + stmt.table_name + "' not found"};

    TableID tid = static_cast<TableID>(
        std::hash<std::string>{}(stmt.table_name) & 0x7FFFFFFF);

    size_t deleted = 0;
    auto col_names = _catalog.column_names(stmt.table_name);

    // collect keys to delete first, then delete (to avoid iterator invalidation)
    std::vector<Key> keys_to_delete;

    _storage.full_scan(tid, [&](const Key& key, const Record& rec) {
        if (evaluate_where(rec, col_names, stmt.where)) {
            keys_to_delete.push_back(key);
        }
    });

    for (const auto& key : keys_to_delete) {
        _storage.remove_direct(key);
        _catalog.decrement_row_count(stmt.table_name);
        ++deleted;
    }

    return {true, "", {}, {}, deleted};
}

inline int QueryExecutor::get_column_index(
    const std::vector<std::string>& col_names, const std::string& name) const {
    for (size_t i = 0; i < col_names.size(); ++i) {
        if (col_names[i] == name) return static_cast<int>(i);
    }
    return -1;
}

inline bool QueryExecutor::evaluate_where(
    const Record& rec, const std::vector<std::string>& col_names,
    const WhereClause& where) {

    if (!where.has_where) return true;

    int idx = get_column_index(col_names, where.column);
    if (idx < 0 || static_cast<size_t>(idx) >= rec.size()) return false;

    const Value& lhs = rec[idx];
    const Value& rhs = where.value;

    if (where.op == "=")  return lhs == rhs;
    if (where.op == "!=") return lhs != rhs;
    if (where.op == "<")  return lhs < rhs;
    if (where.op == ">")  return lhs > rhs;
    if (where.op == "<=") return lhs <= rhs;
    if (where.op == ">=") return lhs >= rhs;

    return false;
}

} // namespace minidb
