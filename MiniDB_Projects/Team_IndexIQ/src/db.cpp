#include "db.h"
#include "parser/lexer.h"
#include "parser/parser.h"
#include <filesystem>
#include <iostream>
#include <sstream>
#include <unordered_set>

namespace fs = std::filesystem;

Database::Database(const std::string& data_dir)
    : data_dir_(data_dir)
    , catalog_(data_dir + "/catalog.txt")
    , wal_(data_dir + "/wal.log")
    , optimizer_(catalog_)
    , executor_(catalog_, heap_files_, indexes_, pool_)
{
    fs::create_directories(data_dir);

    for (auto& [name, _] : catalog_.all()) {
        open_table(name);
        rebuild_index(name);
    }

    recover();
}

Database::~Database() {
    for (auto& [name, f] : heap_files_) { pool_.flush_all(*f); delete f; }
    for (auto& [name, t] : indexes_)    delete t;
}

void Database::open_table(const std::string& name) {
    if (!heap_files_.count(name))
        heap_files_[name] = new HeapFile(data_dir_ + "/" + name + ".db");
    if (!indexes_.count(name))
        indexes_[name] = new BTree();
}

void Database::rebuild_index(const std::string& name) {
    const auto& schema = catalog_.get_schema(name);
    auto* file    = heap_files_.at(name);
    auto* idx     = indexes_.at(name);
    int   spp     = schema.slots_per_page();
    int   rec_sz  = schema.record_size();
    uint32_t pages = file->total_pages();

    for (uint32_t p = 0; p < pages; p++) {
        Page& page = pool_.get_page(*file, p);
        for (int s = 0; s < spp; s++) {
            int off = s * (1 + rec_sz);
            if (page.data[off] == 0) continue;
            Row row = decode_record(page.data + off + 1, schema);
            int pk  = std::stoi(row[0]);
            int rid = static_cast<int>(p) * spp + s;
            idx->insert(pk, rid);
        }
    }
}

void Database::recover() {
    auto records = wal_.read_all();

    std::unordered_set<uint64_t> committed;
    for (auto& r : records)
        if (r.op == "COMMIT") committed.insert(r.txn_id);

    for (auto& r : records) {
        if (!committed.count(r.txn_id)) continue;
        if (r.op == "INSERT") {
            if (!catalog_.has_table(r.table)) continue;
            Row values;
            std::istringstream ss(r.value);
            std::string tok;
            while (std::getline(ss, tok, ',')) values.push_back(tok);
            executor_.recover_insert(r.table, values);
        } else if (r.op == "DELETE") {
            if (!catalog_.has_table(r.table) || r.key.empty()) continue;
            executor_.recover_delete(r.table, std::stoi(r.key));
        }
    }

    for (auto& [name, _] : catalog_.all())
        executor_.flush(name);
}

TxID Database::auto_begin() {
    TxID xid = txn_mgr_.begin();
    wal_.log(xid, "BEGIN");
    return xid;
}

void Database::auto_commit(TxID xid) {
    for (auto& [name, _] : heap_files_) executor_.flush(name);
    wal_.log(xid, "COMMIT");
    txn_mgr_.commit(xid);
}

std::vector<Row> Database::handle_create(const CreateStmt& s) {
    if (catalog_.has_table(s.table))
        throw std::runtime_error("Table already exists: " + s.table);

    TableSchema schema;
    schema.name = s.table;
    for (auto& c : s.columns) schema.columns.push_back({c.name, c.type});
    catalog_.add_table(schema);
    open_table(s.table);
    return {};
}

std::vector<Row> Database::handle_insert(const InsertStmt& s) {
    bool single_stmt = (current_txn_ == 0);
    TxID xid = single_stmt ? auto_begin() : current_txn_;

    txn_mgr_.acquire_lock(s.table, xid, LockMode::EXCLUSIVE);

    std::string val_str;
    for (int i = 0; i < (int)s.values.size(); i++) {
        if (i) val_str += ',';
        val_str += s.values[i];
    }
    wal_.log(xid, "INSERT", s.table, s.values[0], val_str);

    executor_.execute_insert(s.table, s.values);

    if (single_stmt) auto_commit(xid);
    return {};
}

std::vector<Row> Database::handle_delete(const DeleteStmt& s) {
    bool single_stmt = (current_txn_ == 0);
    TxID xid = single_stmt ? auto_begin() : current_txn_;

    txn_mgr_.acquire_lock(s.table, xid, LockMode::EXCLUSIVE);

    auto pks = executor_.matching_pks(s.table, s.where);
    for (int pk : pks)
        wal_.log(xid, "DELETE", s.table, std::to_string(pk), "");

    executor_.execute_delete(s.table, s.where);

    if (single_stmt) auto_commit(xid);
    return {};
}

std::vector<Row> Database::handle_select(const SelectStmt& s) {
    bool single_stmt = (current_txn_ == 0);
    TxID xid = single_stmt ? auto_begin() : current_txn_;

    txn_mgr_.acquire_lock(s.table, xid, LockMode::SHARED);

    QueryPlan plan = optimizer_.plan(s);

    if (s.explain) {
        Row info;
        std::string desc;
        if (plan.scan == ScanType::INDEX_SCAN)
            desc = "INDEX SCAN on " + plan.table + " using " +
                   catalog_.get_schema(plan.table).columns[0].name + " = " +
                   std::to_string(plan.pk_val);
        else
            desc = "TABLE SCAN on " + plan.table;
        if (plan.has_join)
            desc += " + NESTED LOOP JOIN " + plan.join_table;
        info.push_back(desc);
        if (single_stmt) { txn_mgr_.release_locks(xid); txn_mgr_.commit(xid); }
        return {info};
    }

    auto result = executor_.execute_select(plan, s);
    if (single_stmt) { txn_mgr_.release_locks(xid); txn_mgr_.commit(xid); }
    return result;
}

std::vector<Row> Database::execute(const std::string& sql) {
    Lexer lex(sql);
    Parser parser(lex.tokenize());
    Statement stmt = parser.parse();

    return std::visit([&](auto&& s) -> std::vector<Row> {
        using T = std::decay_t<decltype(s)>;
        if constexpr (std::is_same_v<T, CreateStmt>)   return handle_create(s);
        if constexpr (std::is_same_v<T, InsertStmt>)   return handle_insert(s);
        if constexpr (std::is_same_v<T, DeleteStmt>)   return handle_delete(s);
        if constexpr (std::is_same_v<T, SelectStmt>)   return handle_select(s);
        if constexpr (std::is_same_v<T, BeginStmt>) {
            current_txn_ = txn_mgr_.begin();
            wal_.log(current_txn_, "BEGIN");
            return {};
        }
        if constexpr (std::is_same_v<T, CommitStmt>) {
            if (current_txn_ == 0) throw std::runtime_error("No active transaction");
            for (auto& [name, _] : heap_files_) executor_.flush(name);
            wal_.log(current_txn_, "COMMIT");
            txn_mgr_.commit(current_txn_);
            current_txn_ = 0;
            return {};
        }
        if constexpr (std::is_same_v<T, RollbackStmt>) {
            if (current_txn_ == 0) throw std::runtime_error("No active transaction");
            wal_.log(current_txn_, "ABORT");
            txn_mgr_.abort(current_txn_);
            current_txn_ = 0;
            return {};
        }
        return {};
    }, stmt);
}
