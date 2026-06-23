#include "MVCCStore.h"

void MVCCStore::write(const RecordKey& key, const std::string& value, TxnId txn_id, Timestamp begin_ts) {
    auto& versions = store[key];
    // Invalidate the previous version created by this same transaction (if any)
    for (auto& v : versions) {
        if (v.created_by == txn_id && v.end_ts == INF_TS) {
            v.end_ts = begin_ts;
            break;
        }
    }
    versions.push_back({value, txn_id, begin_ts, INF_TS});
}

std::optional<std::string> MVCCStore::read(const RecordKey& key, Timestamp read_ts) const {
    auto it = store.find(key);
    if (it == store.end()) return std::nullopt;
    for (const auto& v : it->second) {
        if (v.begin_ts <= read_ts && v.end_ts > read_ts)
            return v.value;
    }
    return std::nullopt;
}

void MVCCStore::abort_txn(TxnId txn_id) {
    for (auto& [key, versions] : store) {
        // Collect begin timestamps of versions created by this txn
        std::vector<Timestamp> aborted_begin_ts;
        for (auto& v : versions)
            if (v.created_by == txn_id)
                aborted_begin_ts.push_back(v.begin_ts);

        // Restore end_ts on versions that were invalidated by this txn's writes
        for (auto& v : versions) {
            for (Timestamp ts : aborted_begin_ts) {
                if (v.end_ts == ts && v.created_by != txn_id) {
                    v.end_ts = INF_TS;
                    break;
                }
            }
        }

        // Remove versions created by this txn
        auto new_end = std::remove_if(versions.begin(), versions.end(),
                                      [txn_id](const Version& v){ return v.created_by == txn_id; });
        versions.erase(new_end, versions.end());
    }
}

void MVCCStore::print_versions(const RecordKey& key) const {
    auto it = store.find(key);
    if (it == store.end()) { std::cout << "  (no versions for " << key << ")\n"; return; }
    for (const auto& v : it->second) {
        std::cout << "  [" << key << "] val=" << v.value
                  << " txn=" << v.created_by
                  << " begin=" << v.begin_ts
                  << " end=" << (v.end_ts == INF_TS ? "INF" : std::to_string(v.end_ts))
                  << "\n";
    }
}
