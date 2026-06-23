#include "recovery_mgr.h"
#include "../database.h"
#include "../storage/page.h"
#include <unordered_set>
#include <regex>
#include <algorithm>
#include <iostream>

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

static int get_pk_value(const std::string& rec_str) {
    if (rec_str.empty()) return 0;
    Record record = deserialize_rec(rec_str);
    auto it = record.find("id");
    if (it != record.end()) {
        return std::stoi(it->second);
    } else if (!record.empty()) {
        return std::stoi(record.begin()->second);
    }
    return 0;
}

static void apply_update(Database* db, const LogRecord& rec, bool undo) {
    std::string table = rec.table_name;
    int page_id = rec.page_id;
    int slot_id = rec.slot_id;
    std::string target_image = undo ? rec.before_image : rec.after_image;

    if (db->indexes.find(table) == db->indexes.end()) {
        db->indexes[table] = std::unique_ptr<BPlusTree>(new BPlusTree(4));
        db->table_stats[table] = TableStats{0, "id"};
    }

    auto res = db->buffer_pool.fetch_page(table, page_id);
    Page page(page_id, res.second);

    if (target_image.empty()) {
        page.delete_record(slot_id);
        db->buffer_pool.unpin_page(table, page_id, true);

        auto idx_it = db->indexes.find(table);
        if (idx_it != db->indexes.end() && idx_it->second) {
            std::string rec_to_delete = undo ? rec.after_image : rec.before_image;
            int pk = get_pk_value(rec_to_delete);
            idx_it->second->delete_key(pk);
        }
    } else {
        page.insert_record_at(slot_id, target_image);
        db->buffer_pool.unpin_page(table, page_id, true);

        auto idx_it = db->indexes.find(table);
        if (idx_it != db->indexes.end() && idx_it->second) {
            int pk = get_pk_value(target_image);
            idx_it->second->insert(pk, {page_id, slot_id});
        }
    }
}

RecoveryManager::RecoveryManager(Database* db) : db(db) {}

void RecoveryManager::recover() {
    if (!db || !db->wal) return;

    std::vector<LogRecord> records = db->wal->read_all_records();
    if (records.empty()) return;

    // 1. Analysis Phase: Find uncommitted transactions
    std::unordered_set<int> active_txns;
    for (const auto& rec : records) {
        if (rec.type == "BEGIN") {
            active_txns.insert(rec.txn_id);
        } else if (rec.type == "COMMIT" || rec.type == "ABORT") {
            active_txns.erase(rec.txn_id);
        }
    }

    // 2. Redo Phase: Replay all modifications (Redo history)
    for (const auto& rec : records) {
        if (rec.type == "UPDATE") {
            apply_update(db, rec, false);
        }
    }

    // 3. Undo Phase: Rollback updates for all active (uncommitted) transactions
    for (auto it = records.rbegin(); it != records.rend(); ++it) {
        const auto& rec = *it;
        if (rec.type == "UPDATE" && active_txns.find(rec.txn_id) != active_txns.end()) {
            apply_update(db, rec, true);
        }
    }

    // After recovery, write ABORT records for the uncommitted transactions that we rolled back
    for (int txn_id : active_txns) {
        db->wal->log_abort(txn_id);
    }
}

void RecoveryManager::rollback_transaction(int txn_id) {
    if (!db || !db->wal) return;

    std::vector<LogRecord> records = db->wal->read_all_records();
    for (auto it = records.rbegin(); it != records.rend(); ++it) {
        const auto& rec = *it;
        if (rec.txn_id == txn_id && rec.type == "UPDATE") {
            apply_update(db, rec, true);
        }
    }
}
