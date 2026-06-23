#include "replication/replica.h"
#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <cstring>

Replica::~Replica() {
    close();
}

bool Replica::open(const std::string& replication_log_path, TableProvider provider) {
    table_provider_ = provider;
    fd_ = ::open(replication_log_path.c_str(), O_RDONLY);
    if (fd_ < 0) {
        std::cerr << "[Replica] No replication log yet. Will retry.\n";
        return false;
    }
    std::cout << "[Replica] Opened replication log: " << replication_log_path << "\n";
    return true;
}

void Replica::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

int Replica::applyNewRecords() {
    if (fd_ < 0) {
        return 0;
    }

    if (::lseek(fd_, read_offset_, SEEK_SET) < 0) {
        return 0;
    }

    int applied = 0;
    WALRecord rec;

    while (::read(fd_, &rec, WAL_RECORD_SIZE) == static_cast<ssize_t>(WAL_RECORD_SIZE)) {
        replica_lsn_ = rec.lsn;
        read_offset_ += WAL_RECORD_SIZE;

        if (rec.type == WALRecordType::COMMIT) {
            committed_txids_.insert(rec.txid);

            const auto it = pending_records_.find(rec.txid);
            if (it != pending_records_.end()) {
                for (const auto& pr : it->second) {
                    const std::string table_name(pr.table_name);
                    auto [heap, index] = table_provider_(table_name);
                    if (!heap) {
                        continue;
                    }

                    if (pr.type == WALRecordType::INSERT) {
                        Record record{pr.key, std::string(pr.value)};
                        const RecordID rid = heap->insertRecord(record);
                        if (index && rid.isValid()) {
                            index->insert(pr.key, rid);
                        }
                        std::cout << "[Replica] Applied INSERT table=" << table_name
                                  << " key=" << pr.key << "\n";
                        applied++;
                    } else if (pr.type == WALRecordType::DELETE) {
                        if (index) {
                            const auto rid_opt = index->search(pr.key);
                            if (rid_opt) {
                                heap->deleteRecord(*rid_opt);
                                index->remove(pr.key);
                                std::cout << "[Replica] Applied DELETE table=" << table_name
                                          << " key=" << pr.key << "\n";
                                applied++;
                            }
                        }
                    }
                }
                pending_records_.erase(it);
            }
        } else if (rec.type == WALRecordType::INSERT || rec.type == WALRecordType::DELETE) {
            pending_records_[rec.txid].push_back(rec);
        }
    }

    return applied;
}

void Replica::printStatus(LSN primary_lsn) const {
    const LSN lag = (primary_lsn > replica_lsn_) ? (primary_lsn - replica_lsn_) : 0;
    std::cout << "[Replica] Status: replica_lsn=" << replica_lsn_
              << "  primary_lsn=" << primary_lsn
              << "  lag=" << lag << " record(s)\n";
}
