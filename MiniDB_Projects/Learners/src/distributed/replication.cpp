#include "replication.h"
#include <iostream>

ReplicationManager::ReplicationManager(Node* primary) : primary(primary) {}

void ReplicationManager::add_replica(Node* replica) {
    replicas.push_back(replica);
}

void ReplicationManager::replicate() {
    if (!primary || !primary->is_online || !primary->db || !primary->db->wal) {
        return;
    }

    std::vector<LogRecord> prim_records = primary->db->wal->read_all_records();

    for (auto* replica : replicas) {
        if (!replica || !replica->is_online || !replica->db || !replica->db->wal) {
            continue;
        }

        std::vector<LogRecord> repl_records = replica->db->wal->read_all_records();
        int repl_max_lsn = 0;
        if (!repl_records.empty()) {
            repl_max_lsn = repl_records.back().lsn;
        }

        bool has_new = false;
        for (const auto& rec : prim_records) {
            if (rec.lsn > repl_max_lsn) {
                has_new = true;
                if (rec.type == "BEGIN") {
                    replica->db->wal->log_begin(rec.txn_id);
                } else if (rec.type == "COMMIT") {
                    replica->db->wal->log_commit(rec.txn_id);
                } else if (rec.type == "ABORT") {
                    replica->db->wal->log_abort(rec.txn_id);
                } else if (rec.type == "UPDATE") {
                    replica->db->wal->log_update(
                        rec.txn_id, rec.table_name, rec.page_id, rec.slot_id, 
                        rec.before_image, rec.after_image
                    );
                }
            }
        }

        if (has_new) {
            // Replay new log records to apply changes to replica
            replica->db->recovery_mgr->recover();
        }
    }
}
