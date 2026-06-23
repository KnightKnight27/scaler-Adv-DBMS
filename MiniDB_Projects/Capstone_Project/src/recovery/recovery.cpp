#include "recovery/recovery.h"
#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <cstring>

RecoveryResult Recovery::runRedo(const std::string& wal_path, TableProvider table_provider) {
    RecoveryResult result;

    const int fd = ::open(wal_path.c_str(), O_RDONLY);
    if (fd < 0) {
        std::cout << "[Recovery] No WAL file found at '" << wal_path << "' — starting fresh.\n";
        return result;
    }

    std::cout << "[Recovery] Scanning WAL: " << wal_path << "\n";

    std::unordered_set<TxID> committed;
    std::unordered_set<TxID> aborted;
    std::vector<WALRecord> all_records;

    WALRecord rec;
    while (::read(fd, &rec, WAL_RECORD_SIZE) == static_cast<ssize_t>(WAL_RECORD_SIZE)) {
        all_records.push_back(rec);
        if (rec.lsn > result.last_lsn) {
            result.last_lsn = rec.lsn;
        }

        if (rec.type == WALRecordType::COMMIT) {
            committed.insert(rec.txid);
            result.txns_committed++;
        } else if (rec.type == WALRecordType::ABORT) {
            aborted.insert(rec.txid);
            result.txns_aborted++;
        }
    }

    std::cout << "[Recovery] Pass 1 complete: "
              << committed.size() << " committed txns, "
              << aborted.size() << " aborted txns, "
              << all_records.size() << " total records.\n";

    std::cout << "[Recovery] Pass 2: replaying committed transactions...\n";

    for (const auto& r : all_records) {
        if (r.type != WALRecordType::INSERT && r.type != WALRecordType::DELETE) {
            continue;
        }

        if (committed.find(r.txid) == committed.end()) {
            continue;
        }

        const std::string table_name(r.table_name);
        auto [heap, index] = table_provider(table_name);

        if (!heap) {
            std::cout << "[Recovery]   WARNING: Table '" << table_name
                      << "' not found, skipping LSN=" << r.lsn << "\n";
            continue;
        }

        if (r.type == WALRecordType::INSERT) {
            Record record{r.key, std::string(r.value)};
            const RecordID rid = heap->insertRecord(record);
            if (index && rid.isValid()) {
                index->insert(r.key, rid);
            }
            result.records_redone++;
            std::cout << "[Recovery]   REDO INSERT table=" << table_name
                      << " key=" << r.key << " value=" << r.value
                      << " (LSN=" << r.lsn << ")\n";
        } else if (r.type == WALRecordType::DELETE) {
            if (index) {
                const auto rid_opt = index->search(r.key);
                if (rid_opt) {
                    heap->deleteRecord(*rid_opt);
                    index->remove(r.key);
                    result.records_redone++;
                    std::cout << "[Recovery]   REDO DELETE table=" << table_name
                              << " key=" << r.key << " (LSN=" << r.lsn << ")\n";
                }
            }
        }
    }

    ::close(fd);
    return result;
}

void Recovery::printResult(const RecoveryResult& result) {
    std::cout << "\n╔══════════════════════════════════════╗\n";
    std::cout << "║         RECOVERY COMPLETE             ║\n";
    std::cout << "╠══════════════════════════════════════╣\n";
    std::cout << "║  Committed transactions : " << result.txns_committed << "\n";
    std::cout << "║  Aborted transactions   : " << result.txns_aborted << "\n";
    std::cout << "║  Records redone         : " << result.records_redone << "\n";
    std::cout << "║  Highest LSN seen       : " << result.last_lsn << "\n";
    std::cout << "╚══════════════════════════════════════╝\n\n";
}
