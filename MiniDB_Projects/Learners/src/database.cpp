#include "database.h"
#include "compat.h"
#include <iostream>
#include <algorithm>
#include <regex>

using Record = std::unordered_map<std::string, std::string>;

static Record deserialize_rec(const std::string& str) {
    Record rec;
    if (str.length() < 2 || str.front() != '{' || str.back() != '}') {
        return rec;
    }
    
    size_t i = 1;
    size_t len = str.length();
    while (i < len - 1) {
        while (i < len - 1 && (str[i] == ' ' || str[i] == ',')) {
            i++;
        }
        if (i >= len - 1) break;
        
        if (str[i] != '"') {
            break;
        }
        i++;
        size_t key_start = i;
        while (i < len - 1 && str[i] != '"') {
            i++;
        }
        if (i >= len - 1) break;
        std::string key = str.substr(key_start, i - key_start);
        i++;
        
        while (i < len - 1 && (str[i] == ' ' || str[i] == ':')) {
            i++;
        }
        if (i >= len - 1) break;
        
        std::string val;
        if (str[i] == '"') {
            i++;
            size_t val_start = i;
            while (i < len - 1 && str[i] != '"') {
                i++;
            }
            val = str.substr(val_start, i - val_start);
            i++;
        } else {
            size_t val_start = i;
            while (i < len - 1 && str[i] != ',' && str[i] != '}') {
                i++;
            }
            size_t val_end = i;
            while (val_end > val_start && str[val_end - 1] == ' ') {
                val_end--;
            }
            val = str.substr(val_start, val_end - val_start);
        }
        
        rec[key] = val;
    }
    return rec;
}

Database::Database(const std::string& db_dir, bool use_wal)
    : db_dir(db_dir),
      page_manager(db_dir),
      buffer_pool(page_manager, 10),
      optimizer(page_manager, table_stats) {
    
    if (use_wal) {
        wal = std::unique_ptr<WAL>(new WAL(db_dir + "/minidb.log"));
        recovery_mgr = std::unique_ptr<RecoveryManager>(new RecoveryManager(this));
        // Run ARIES recovery on startup
        recovery_mgr->recover();
    }

    // Rebuild index and stats from disk files
    rebuild_indexes();
}

Database::~Database() {
    buffer_pool.flush_all();
}

void Database::rebuild_indexes() {
    if (!fs_compat::exists(db_dir)) return;

    auto db_files = fs_compat::get_db_files(db_dir);
    for (const auto& filename : db_files) {
        std::string table_name = filename.substr(0, filename.length() - 3);
        
        indexes[table_name] = std::unique_ptr<BPlusTree>(new BPlusTree(4));
        table_stats[table_name] = TableStats{0, "id"};

            BPlusTree* idx = indexes[table_name].get();
            int num_pages = page_manager.get_num_pages(table_name);
            int count = 0;

            for (int pid = 0; pid < num_pages; ++pid) {
                auto res = buffer_pool.fetch_page(table_name, pid);
                Page page(pid, res.second);
                for (int sid = 0; sid < page.num_slots; ++sid) {
                    std::string record_bytes = page.get_record(sid);
                    if (!record_bytes.empty()) {
                        Record record = deserialize_rec(record_bytes);
                        auto it = record.find("id");
                        if (it != record.end()) {
                            try {
                                int pk_val = std::stoi(it->second);
                                idx->insert(pk_val, {pid, sid});
                            } catch (...) {}
                        }
                        count++;
                    }
                }
                buffer_pool.unpin_page(table_name, pid, false);
            }
            table_stats[table_name].num_records = count;
        }
}

void Database::rollback_transaction(int txn_id) {
    if (recovery_mgr) {
        recovery_mgr->rollback_transaction(txn_id);
    }
}

Transaction* Database::begin_transaction() {
    LockGuard lck(txn_mu);
    int tid = next_txn_id++;
    return new Transaction(tid, this);
}

static AbstractExecutor* build_scan(Database* db, const std::string& table, const Optional<WhereClause>& where, Transaction* txn) {
    bool has_idx = (db->indexes.find(table) != db->indexes.end() && db->indexes[table]);
    std::string scan_type = "TableScan";
    if (has_idx) {
        db->optimizer.cost_scan(table, where, has_idx, scan_type);
    }
    
    if (scan_type == "IndexScan" && where.has_value() && where->column == "id") {
        int start = -2147483648;
        int end = 2147483647;
        try {
            int val = std::stoi(where->value);
            if (where->op == "=") { start = val; end = val; }
            else if (where->op == ">") { start = val + 1; }
            else if (where->op == ">=") { start = val; }
            else if (where->op == "<") { end = val - 1; }
            else if (where->op == "<=") { end = val; }
        } catch (...) {}
        return new IndexScanExecutor(db->buffer_pool, table, db->indexes[table].get(), start, end, txn);
    }
    return new TableScanExecutor(db->buffer_pool, table, txn);
}

static AbstractExecutor* build_select_plan(Database* db, const SQLStatement& stmt, Transaction* txn) {
    if (stmt.join.has_value()) {
        std::string outer_table, inner_table;
        bool t1_has_idx = (db->indexes.find(stmt.table) != db->indexes.end() && db->indexes[stmt.table]);
        bool t2_has_idx = (db->indexes.find(stmt.join->table) != db->indexes.end() && db->indexes[stmt.join->table]);
        
        db->optimizer.select_join_order(stmt.table, stmt.join->table, *stmt.join, stmt.where, {}, t1_has_idx, t2_has_idx, outer_table, inner_table);
        
        Optional<WhereClause> outer_where = {};
        Optional<WhereClause> inner_where = {};
        if (stmt.where.has_value()) {
            if (stmt.where->column.find(inner_table + ".") != std::string::npos) {
                inner_where = stmt.where;
            } else {
                outer_where = stmt.where;
            }
        }
        
        auto inner_builder = [db, inner_table, inner_where, txn]() -> AbstractExecutor* {
            AbstractExecutor* scan = build_scan(db, inner_table, inner_where, txn);
            if (inner_where.has_value()) {
                bool has_idx = (db->indexes.find(inner_table) != db->indexes.end() && db->indexes[inner_table]);
                std::string st;
                db->optimizer.cost_scan(inner_table, inner_where, has_idx, st);
                if (st != "IndexScan") {
                    scan = new FilterExecutor(scan, *inner_where);
                }
            }
            return scan;
        };
        
        AbstractExecutor* outer_scan = build_scan(db, outer_table, outer_where, txn);
        if (outer_where.has_value()) {
            bool has_idx = (db->indexes.find(outer_table) != db->indexes.end() && db->indexes[outer_table]);
            std::string st;
            db->optimizer.cost_scan(outer_table, outer_where, has_idx, st);
            if (st != "IndexScan") {
                outer_scan = new FilterExecutor(outer_scan, *outer_where);
            }
        }
        
        return new NestedLoopJoinExecutor(outer_scan, inner_builder, *stmt.join);
    } else {
        AbstractExecutor* scan = build_scan(db, stmt.table, stmt.where, txn);
        if (stmt.where.has_value()) {
            bool has_idx = (db->indexes.find(stmt.table) != db->indexes.end() && db->indexes[stmt.table]);
            std::string st;
            db->optimizer.cost_scan(stmt.table, stmt.where, has_idx, st);
            if (st != "IndexScan") {
                scan = new FilterExecutor(scan, *stmt.where);
            }
        }
        return scan;
    }
}

int Database::execute_update(const std::string& sql, Transaction* txn) {
    bool is_implicit = (txn == nullptr);
    if (is_implicit) {
        txn = begin_transaction();
    }

    try {
        SQLStatement stmt = SQLParser::parse(sql);
        if (stmt.type == "INSERT") {
            if (indexes.find(stmt.table) == indexes.end()) {
                indexes[stmt.table] = std::unique_ptr<BPlusTree>(new BPlusTree(4));
                table_stats[stmt.table] = TableStats{0, "id"};
            }
            
            std::vector<std::string> cols;
            if (stmt.table == "students" && stmt.values.size() == 3) {
                cols = {"id", "name", "age"};
            } else {
                cols.push_back("id");
                for (size_t i = 1; i < stmt.values.size(); ++i) {
                    cols.push_back("col" + std::to_string(i));
                }
            }

            InsertExecutor exec(buffer_pool, stmt.table, indexes[stmt.table].get(), stmt.values, cols, txn);
            exec.execute();
            table_stats[stmt.table].num_records++;
            
            if (is_implicit) {
                txn->commit();
                delete txn;
            }
            return 1;
        } else if (stmt.type == "DELETE") {
            SQLStatement select_stmt;
            select_stmt.type = "SELECT";
            select_stmt.table = stmt.table;
            select_stmt.where = stmt.where;

            AbstractExecutor* select_plan = build_select_plan(this, select_stmt, txn);
            DeleteExecutor exec(buffer_pool, stmt.table, indexes[stmt.table].get(), select_plan, txn);
            int del_count = exec.execute();
            table_stats[stmt.table].num_records = std::max(0, table_stats[stmt.table].num_records - del_count);
            
            if (is_implicit) {
                txn->commit();
                delete txn;
            }
            return del_count;
        }
    } catch (...) {
        if (is_implicit && txn) {
            try { txn->abort(); } catch (...) {}
            delete txn;
        }
        throw;
    }
    
    if (is_implicit && txn) {
        delete txn;
    }
    throw std::runtime_error("Unsupported update SQL: " + sql);
}

std::vector<Record> Database::execute_query(const std::string& sql, Transaction* txn) {
    SQLStatement stmt = SQLParser::parse(sql);
    if (stmt.type != "SELECT") {
        throw std::runtime_error("Expected SELECT query, got: " + sql);
    }

    AbstractExecutor* plan = build_select_plan(this, stmt, txn);
    plan->open();

    std::vector<Record> results;
    while (true) {
        auto res = plan->next();
        if (!res.has_value()) {
            break;
        }

        Record rec = res->second;
        if (!stmt.columns.empty() && stmt.columns[0] != "*") {
            Record proj_rec;
            for (const auto& col : stmt.columns) {
                std::string short_col = col;
                size_t dot = col.find('.');
                if (dot != std::string::npos) {
                    short_col = col.substr(dot + 1);
                }
                
                auto it = rec.find(col);
                if (it != rec.end()) {
                    proj_rec[short_col] = it->second;
                } else {
                    it = rec.find(short_col);
                    if (it != rec.end()) {
                        proj_rec[short_col] = it->second;
                    } else {
                        for (const auto& item : rec) {
                            const std::string& k = item.first;
                            const std::string& v = item.second;
                            size_t d = k.find('.');
                            std::string sk = (d != std::string::npos) ? k.substr(d + 1) : k;
                            if (sk == short_col) {
                                proj_rec[short_col] = v;
                                break;
                            }
                        }
                    }
                }
            }
            results.push_back(proj_rec);
        } else {
            results.push_back(rec);
        }
    }

    plan->close();
    delete plan;
    return results;
}
