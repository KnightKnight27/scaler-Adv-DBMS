#include "recovery/recovery_manager.h"

#include <unordered_set>
#include <vector>

namespace minidb {

std::size_t RecoveryManager::recover(StorageEngine* engine, const LogManager& log) {
    std::vector<LogRecord> recs = log.read_all();

    // Analysis: which transactions committed?
    std::unordered_set<TxId> committed;
    for (const LogRecord& r : recs)
        if (r.type == LogType::COMMIT) committed.insert(r.tx);

    // Redo committed operations in order; skip losers (rolls them back).
    std::size_t replayed = 0;
    for (const LogRecord& r : recs) {
        if (r.type == LogType::COMMIT || !committed.count(r.tx)) continue;
        if (r.type == LogType::PUT)        engine->put(r.table, r.key, r.row);
        else if (r.type == LogType::ERASE) engine->erase(r.table, r.key);
        ++replayed;
    }
    return replayed;
}

} // namespace minidb
