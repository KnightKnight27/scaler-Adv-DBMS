#include "executor.h"
#include "../tx/transaction.h"
#include "../database.h"
#include <sstream>
#include <regex>
#include <iostream>

// Helper JSON Serializer
static std::string serialize_record(const Record& rec) {
    std::stringstream ss;
    ss << "{";
    bool first = true;
    for (const auto& item : rec) {
        const std::string& k = item.first;
        const std::string& v = item.second;
        if (!first) ss << ", ";
        first = false;
        ss << "\"" << k << "\": ";
        // Check if value is numeric
        bool is_num = !v.empty() && std::all_of(v.begin(), v.end(), [](char c) {
            return std::isdigit(c) || c == '.';
        });
        if (is_num) {
            ss << v;
        } else {
            ss << "\"" << v << "\"";
        }
    }
    ss << "}";
    return ss.str();
}

static Record deserialize_record(const std::string& str) {
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

// ──────────────────────────────────────────────────────────────────────
// TableScanExecutor
// ──────────────────────────────────────────────────────────────────────
TableScanExecutor::TableScanExecutor(BufferPool& bp, const std::string& table_name, Transaction* txn)
    : buffer_pool(bp), table_name(table_name), txn(txn) {}

void TableScanExecutor::open() {
    num_pages = buffer_pool.page_manager.get_num_pages(table_name);
    curr_page_id = 0;
    curr_slot_id = 0;
    curr_page_data = nullptr;
    curr_page = nullptr;
}

Optional<std::pair<RID, Record>> TableScanExecutor::next() {
    while (curr_page_id < num_pages) {
        if (curr_page_data == nullptr) {
            auto res = buffer_pool.fetch_page(table_name, curr_page_id);
            curr_page_data = res.second;
            curr_page = std::make_shared<Page>(curr_page_id, curr_page_data);
            curr_slot_id = 0;
        }

        while (curr_slot_id < curr_page->num_slots) {
            int slot_id = curr_slot_id++;
            std::string rec_str = curr_page->get_record(slot_id);
            if (!rec_str.empty()) {
                RID rid = {curr_page_id, slot_id};
                if (txn) {
                    txn->acquire_shared(table_name, rid);
                }
                return std::make_pair(rid, deserialize_record(rec_str));
            }
        }

        // Unpin current page
        buffer_pool.unpin_page(table_name, curr_page_id, false);
        curr_page_data = nullptr;
        curr_page = nullptr;
        curr_page_id++;
    }
    return {};
}

void TableScanExecutor::close() {
    if (curr_page_data != nullptr) {
        buffer_pool.unpin_page(table_name, curr_page_id, false);
        curr_page_data = nullptr;
        curr_page = nullptr;
    }
}

// ──────────────────────────────────────────────────────────────────────
// IndexScanExecutor
// ──────────────────────────────────────────────────────────────────────
IndexScanExecutor::IndexScanExecutor(BufferPool& bp, const std::string& table_name, BPlusTree* idx, int start, int end, Transaction* txn)
    : buffer_pool(bp), table_name(table_name), index(idx), start_key(start), end_key(end), txn(txn) {}

void IndexScanExecutor::open() {
    matching_rids = index->range_search(start_key, end_key);
    curr_idx = 0;
}

Optional<std::pair<RID, Record>> IndexScanExecutor::next() {
    if (curr_idx >= matching_rids.size()) {
        return {};
    }

    auto entry = matching_rids[curr_idx++];
    RID rid = entry.second;

    if (txn) {
        txn->acquire_shared(table_name, rid);
    }

    auto res = buffer_pool.fetch_page(table_name, rid.first);
    Page page(rid.first, res.second);
    std::string rec_str = page.get_record(rid.second);
    buffer_pool.unpin_page(table_name, rid.first, false);

    if (!rec_str.empty()) {
        return std::make_pair(rid, deserialize_record(rec_str));
    }
    return next(); // Try next if deleted
}

void IndexScanExecutor::close() {}

// ──────────────────────────────────────────────────────────────────────
// FilterExecutor
// ──────────────────────────────────────────────────────────────────────
FilterExecutor::FilterExecutor(AbstractExecutor* child_exec, const WhereClause& where)
    : child(child_exec), where(where) {}

void FilterExecutor::open() {
    child->open();
}

bool FilterExecutor::matches(const Record& record) const {
    std::string col = where.column;
    // Resolve column prefix
    size_t dot_pos = col.find('.');
    if (dot_pos != std::string::npos) {
        col = col.substr(dot_pos + 1);
    }

    std::string actual_val = "";
    auto it = record.find(col);
    if (it != record.end()) {
        actual_val = it->second;
    } else {
        // Fallback: search key with suffix match
        for (const auto& item : record) {
            const std::string& k = item.first;
            const std::string& v = item.second;
            size_t dot = k.find('.');
            std::string short_k = (dot != std::string::npos) ? k.substr(dot + 1) : k;
            if (short_k == col) {
                actual_val = v;
                break;
            }
        }
    }

    if (actual_val.empty()) return false;

    if (where.op == "=") {
        return actual_val == where.value;
    } else if (where.op == "!=") {
        return actual_val != where.value;
    }
    
    // Convert to double for range checks
    try {
        double act = std::stod(actual_val);
        double exp = std::stod(where.value);
        if (where.op == ">") return act > exp;
        if (where.op == "<") return act < exp;
        if (where.op == ">=") return act >= exp;
        if (where.op == "<=") return act <= exp;
    } catch (...) {
        return false;
    }
    return false;
}

Optional<std::pair<RID, Record>> FilterExecutor::next() {
    while (true) {
        auto res = child->next();
        if (!res.has_value()) {
            return {};
        }
        if (matches(res->second)) {
            return res;
        }
    }
}

void FilterExecutor::close() {
    child->close();
}

// ──────────────────────────────────────────────────────────────────────
// NestedLoopJoinExecutor
// ──────────────────────────────────────────────────────────────────────
NestedLoopJoinExecutor::NestedLoopJoinExecutor(
    AbstractExecutor* outer_exec, 
    std::function<AbstractExecutor*()> inner_exec_builder, 
    const JoinClause& cond
) : outer(outer_exec), inner_builder(inner_exec_builder), join_cond(cond) {}

void NestedLoopJoinExecutor::open() {
    outer->open();
    curr_outer_res = {};
    curr_inner = nullptr;
}

std::string NestedLoopJoinExecutor::get_val(const Record& record, const std::string& col) const {
    std::string short_col = col;
    size_t dot = col.find('.');
    if (dot != std::string::npos) {
        short_col = col.substr(dot + 1);
    }
    auto it = record.find(col);
    if (it != record.end()) return it->second;
    for (const auto& item : record) {
        const std::string& k = item.first;
        const std::string& v = item.second;
        size_t d = k.find('.');
        std::string sk = (d != std::string::npos) ? k.substr(d + 1) : k;
        if (sk == short_col) return v;
    }
    return "";
}

Optional<std::pair<RID, Record>> NestedLoopJoinExecutor::next() {
    while (true) {
        if (!curr_outer_res.has_value()) {
            curr_outer_res = outer->next();
            if (!curr_outer_res.has_value()) {
                return {};
            }
            curr_inner.reset(inner_builder());
            curr_inner->open();
        }

        auto inner_res = curr_inner->next();
        if (!inner_res.has_value()) {
            curr_inner->close();
            curr_inner = nullptr;
            curr_outer_res = {};
            continue;
        }

        std::string out_val = get_val(curr_outer_res->second, join_cond.left_col);
        std::string in_val = get_val(inner_res->second, join_cond.right_col);

        if (!out_val.empty() && !in_val.empty() && out_val == in_val) {
            // Combine records
            Record combined = curr_outer_res->second;
            for (const auto& item : inner_res->second) {
                combined[item.first] = item.second;
            }
            // RID is outer page_id, inner page_id (or similar dummy values)
            RID comb_rid = {curr_outer_res->first.first, inner_res->first.first};
            return std::make_pair(comb_rid, combined);
        }
    }
}

void NestedLoopJoinExecutor::close() {
    outer->close();
    if (curr_inner) {
        curr_inner->close();
    }
}

// ──────────────────────────────────────────────────────────────────────
// InsertExecutor
// ──────────────────────────────────────────────────────────────────────
InsertExecutor::InsertExecutor(
    BufferPool& bp, const std::string& table_name, BPlusTree* idx,
    const std::vector<std::string>& vals, const std::vector<std::string>& cols,
    Transaction* txn
) : buffer_pool(bp), table_name(table_name), index(idx), values(vals), columns(cols), txn(txn) {}

RID InsertExecutor::execute() {
    // Check primary key constraint (assume PK is the first column / index 0)
    int pk_val = std::stoi(values[0]);
    RID dummy;
    if (index && index->search(pk_val, dummy)) {
        throw std::runtime_error("Primary key violation: key already exists.");
    }

    // Zip values into Record dictionary format
    Record record;
    for (size_t i = 0; i < columns.size() && i < values.size(); ++i) {
        record[columns[i]] = values[i];
    }

    std::string record_bytes = serialize_record(record);

    // Find page with space
    int num_pages = buffer_pool.page_manager.get_num_pages(table_name);
    int target_page_id = -1;

    for (int pid = 0; pid < num_pages; ++pid) {
        auto res = buffer_pool.fetch_page(table_name, pid);
        Page page(pid, res.second);
        if (page.has_room_for(record_bytes.length())) {
            target_page_id = pid;
            break;
        }
        buffer_pool.unpin_page(table_name, pid, false);
    }

    if (target_page_id == -1) {
        target_page_id = buffer_pool.page_manager.allocate_page(table_name);
    }

    auto res = buffer_pool.fetch_page(table_name, target_page_id);
    Page page(target_page_id, res.second);
    
    RID rid = {target_page_id, page.num_slots};

    if (txn) {
        txn->acquire_exclusive(table_name, rid);
        // Log to WAL
        if (txn->db && txn->db->wal) {
            txn->db->wal->log_update(txn->txn_id, table_name, target_page_id, rid.second, "", record_bytes);
        }
    }

    int slot_id = page.insert_record(record_bytes);
    buffer_pool.unpin_page(table_name, target_page_id, true);

    rid = {target_page_id, slot_id};
    if (index) {
        index->insert(pk_val, rid);
    }
    return rid;
}

// ──────────────────────────────────────────────────────────────────────
// DeleteExecutor
// ──────────────────────────────────────────────────────────────────────
DeleteExecutor::DeleteExecutor(
    BufferPool& bp, const std::string& table_name, BPlusTree* idx,
    AbstractExecutor* child_exec, Transaction* txn
) : buffer_pool(bp), table_name(table_name), index(idx), child(child_exec), txn(txn) {}

int DeleteExecutor::execute() {
    int count = 0;
    child->open();

    std::vector<std::pair<RID, Record>> rids_to_delete;
    while (true) {
        auto res = child->next();
        if (!res.has_value()) {
            break;
        }
        rids_to_delete.push_back(*res);
    }
    child->close();

    for (const auto& entry : rids_to_delete) {
        RID rid = entry.first;
        Record record = entry.second;

        if (txn) {
            txn->acquire_exclusive(table_name, rid);
        }

        auto res = buffer_pool.fetch_page(table_name, rid.first);
        Page page(rid.first, res.second);
        std::string old_val = page.get_record(rid.second);

        if (txn && txn->db && txn->db->wal) {
            txn->db->wal->log_update(txn->txn_id, table_name, rid.first, rid.second, old_val, "");
        }

        page.delete_record(rid.second);
        buffer_pool.unpin_page(table_name, rid.first, true);

        if (index) {
            // PK is the first key in the record map or lists.
            // Retrieve value of PK column (assume it maps to the first element or contains "id")
            int pk_val = 0;
            auto it = record.find("id");
            if (it != record.end()) {
                pk_val = std::stoi(it->second);
            } else if (!record.empty()) {
                pk_val = std::stoi(record.begin()->second);
            }
            index->delete_key(pk_val);
        }
        count++;
    }

    return count;
}
