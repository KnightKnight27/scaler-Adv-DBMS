#include "Database.hpp"

#include <sstream>

using namespace std;

namespace minidb {

Database::Database()
    : page_manager_(buffer_pool_),
      catalog_(page_manager_),
      optimizer_(catalog_),
      executor_(catalog_),
      wal_("minidb_data/default.wal"),
      recovery_(wal_, catalog_) {}

bool Database::open(const string& name) {
    db_path_ = string(DATA_DIR) + "/" + name;
    wal_.setFilepath(db_path_ + ".wal");
    if (!page_manager_.open(db_path_ + ".db")) return false;
    return wal_.open();
}

void Database::close() {
    page_manager_.flushAll();
    page_manager_.close();
    wal_.close();
}

Row Database::mapInsertValues(const ParsedStatement& stmt, const TableDef& def) {
    Row row;
    for (size_t i = 0; i < def.columns.size(); ++i) {
        string key = "col" + to_string(i + 1);
        if (stmt.insert_row.count(key)) row[def.columns[i].name] = stmt.insert_row.at(key);
    }
    return row;
}

string Database::formatRows(const RowList& rows) {
    if (rows.empty()) return "(empty result)\n";
    ostringstream oss;
    for (const Row& r : rows) {
        oss << "| ";
        for (const auto& kv : r) oss << kv.first << "=" << kv.second << " ";
        oss << "|\n";
    }
    return oss.str();
}

string Database::handleCreateTable(const ParsedStatement& stmt) {
    TableDef def;
    def.name = stmt.table_name;
    for (const ColumnSpec& c : stmt.columns) {
        ColumnDef cd;
        cd.name = c.name;
        cd.type = c.type;
        cd.is_primary_key = c.primary_key;
        if (c.primary_key) def.primary_key_column = c.name;
        def.columns.push_back(cd);
    }
    if (def.primary_key_column.empty() && !def.columns.empty()) {
        def.primary_key_column = def.columns[0].name;
        def.columns[0].is_primary_key = true;
    }
    if (!catalog_.createTable(def)) return "ERROR: table already exists\n";
    return "OK: table " + def.name + " created\n";
}

string Database::handleInsert(const ParsedStatement& stmt, int txn_id) {
    const TableDef* def = catalog_.getTable(stmt.table_name);
    if (!def) return "ERROR: unknown table\n";
    string lock_key = "table:" + stmt.table_name;
    if (txn_id > 0 && !txn_manager_.getLockManager().acquireLock(txn_id, lock_key, LockType::EXCLUSIVE))
        return "ERROR: could not acquire lock\n";

    Row row = mapInsertValues(stmt, *def);
    LogRecord log_rec;
    log_rec.type = LogRecordType::INSERT;
    log_rec.txn_id = txn_id > 0 ? txn_id : 0;
    log_rec.table = stmt.table_name;
    log_rec.row = row;
    wal_.append(log_rec);

    HeapFile* hf = catalog_.getHeapFile(stmt.table_name);
    RowLocation loc = hf->insertRow(row);
    BPlusTree* idx = catalog_.getPrimaryIndex(stmt.table_name);
    if (idx && !def->primary_key_column.empty()) {
        auto pk = row.find(def->primary_key_column);
        if (pk != row.end()) idx->insert(atoi(pk->second.c_str()), loc);
    }
    page_manager_.flushAll();
    if (txn_id > 0) txn_manager_.recordInsert(txn_id, stmt.table_name, row, loc);
    return "OK: 1 row inserted\n";
}

string Database::handleSelect(const ParsedStatement& stmt, int txn_id) {
    string lock_key = "table:" + stmt.table_name;
    if (txn_id > 0 && !txn_manager_.getLockManager().acquireLock(txn_id, lock_key, LockType::SHARED))
        return "ERROR: could not acquire lock\n";
    QueryPlan plan = optimizer_.buildSelectPlan(stmt);
    RowList rows = executor_.executeSelect(plan);
    ostringstream oss;
    oss << "Plan: " << (plan.scan == ScanType::INDEX_SCAN ? "INDEX_SCAN" : "TABLE_SCAN");
    oss << " cost=" << plan.estimated_cost << "\n";
    oss << formatRows(rows);
    return oss.str();
}

string Database::handleDelete(const ParsedStatement& stmt, int txn_id) {
    const TableDef* def = catalog_.getTable(stmt.table_name);
    if (!def) return "ERROR: unknown table\n";
    string lock_key = "table:" + stmt.table_name;
    if (txn_id > 0 && !txn_manager_.getLockManager().acquireLock(txn_id, lock_key, LockType::EXCLUSIVE))
        return "ERROR: could not acquire lock\n";

    HeapFile* hf = catalog_.getHeapFile(stmt.table_name);
    RowList all = hf->scanAll();
    int deleted = 0;
    for (size_t i = 0; i < all.size(); ++i) {
        if (stmt.has_where && !Executor(catalog_).evaluatePredicate(all[i], stmt.where, *def)) continue;
        RowLocation loc{def->heap_file_id, (uint32_t)i};
        LogRecord log_rec;
        log_rec.type = LogRecordType::DELETE;
        log_rec.txn_id = txn_id > 0 ? txn_id : 0;
        log_rec.table = stmt.table_name;
        log_rec.row = all[i];
        log_rec.location = loc;
        wal_.append(log_rec);
        hf->deleteRow(loc);
        if (!def->primary_key_column.empty()) {
            auto pk = all[i].find(def->primary_key_column);
            if (pk != all[i].end()) {
                BPlusTree* idx = catalog_.getPrimaryIndex(stmt.table_name);
                if (idx) idx->remove(atoi(pk->second.c_str()));
            }
        }
        ++deleted;
        if (stmt.has_where) break;
    }
    page_manager_.flushAll();
    return "OK: " + to_string(deleted) + " row(s) deleted\n";
}

string Database::executeSQL(const string& sql, int& current_txn) {
    if (crashed_) return "ERROR: system crashed — run RECOVER first\n";
    Parser parser(sql);
    ParsedStatement stmt = parser.parse();

    switch (stmt.type) {
        case StmtType::CREATE_TABLE: return handleCreateTable(stmt);
        case StmtType::INSERT: return handleInsert(stmt, current_txn);
        case StmtType::SELECT: return handleSelect(stmt, current_txn);
        case StmtType::DELETE: return handleDelete(stmt, current_txn);
        case StmtType::BEGIN: {
            current_txn = txn_manager_.beginTransaction();
            LogRecord rec; rec.type = LogRecordType::BEGIN; rec.txn_id = current_txn;
            wal_.append(rec);
            return "OK: transaction " + to_string(current_txn) + " started\n";
        }
        case StmtType::COMMIT: {
            if (current_txn <= 0) return "ERROR: no active transaction\n";
            LogRecord rec; rec.type = LogRecordType::COMMIT; rec.txn_id = current_txn;
            wal_.append(rec);
            txn_manager_.commit(current_txn);
            page_manager_.flushAll();
            current_txn = 0;
            return "OK: committed\n";
        }
        case StmtType::ROLLBACK: {
            if (current_txn <= 0) return "ERROR: no active transaction\n";
            LogRecord rec; rec.type = LogRecordType::ABORT; rec.txn_id = current_txn;
            wal_.append(rec);
            txn_manager_.rollback(current_txn);
            current_txn = 0;
            return "OK: rolled back\n";
        }
        case StmtType::CRASH:
            crashed_ = true;
            return "OK: simulated crash — run RECOVER\n";
        case StmtType::RECOVER:
            recovery_.recover();
            crashed_ = false;
            return "OK: recovery complete\n";
        default:
            return "ERROR: could not parse SQL\n";
    }
}

}  // namespace minidb
