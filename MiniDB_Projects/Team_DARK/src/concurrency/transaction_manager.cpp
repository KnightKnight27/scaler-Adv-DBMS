#include "concurrency/transaction_manager.h"

#include "recovery/recovery_manager.h"

#include <sys/stat.h>
#include <ctime>
#include <stdexcept>
#include <unistd.h>

namespace minidb {

namespace {

std::string TempDbPath() {
    return std::string("/tmp/minidb_tx_") +
           std::to_string(static_cast<unsigned long>(std::time(nullptr))) + "_" +
           std::to_string(static_cast<unsigned long>(::getpid())) + ".db";
}

bool FileHasData(const std::string& path) {
    struct stat st {};
    return ::stat(path.c_str(), &st) == 0 && st.st_size > 0;
}

}  // namespace

std::string TransactionManager::DeriveLogPath(const std::string& db_path,
                                              const std::string& log_path) {
    if (!log_path.empty()) {
        return log_path;
    }
    if (db_path.size() >= 4 && db_path.substr(db_path.size() - 3) == ".db") {
        return db_path.substr(0, db_path.size() - 3) + ".log";
    }
    return db_path + ".log";
}

TransactionManager::TransactionManager() : next_xid_(1), bpm_(nullptr), durable_mode_(false) {
    db_path_ = TempDbPath();
    log_path_ = DeriveLogPath(db_path_, "");
    owned_disk_ = std::make_unique<DiskManager>(db_path_);
    owned_bpm_ = std::make_unique<BufferPoolManager>(owned_disk_.get(), 32);
    bpm_ = owned_bpm_.get();
    heap_ = std::make_unique<TableHeap>(bpm_);
}

TransactionManager::TransactionManager(BufferPoolManager* bpm)
    : next_xid_(1), bpm_(bpm), heap_(std::make_unique<TableHeap>(bpm)) {
    if (bpm_ == nullptr) {
        throw std::invalid_argument("bpm must not be null");
    }
}

TransactionManager::TransactionManager(const std::string& db_path, const std::string& log_path,
                                       std::size_t pool_size)
    : next_xid_(1), bpm_(nullptr) {
    db_path_ = db_path;
    log_path_ = DeriveLogPath(db_path_, log_path);
    const bool has_existing_data = FileHasData(db_path_);
    const bool has_existing_log = FileHasData(log_path_);

    owned_disk_ = std::make_unique<DiskManager>(db_path_);
    owned_bpm_ = std::make_unique<BufferPoolManager>(owned_disk_.get(), pool_size);
    bpm_ = owned_bpm_.get();
    log_manager_ = std::make_unique<LogManager>(log_path_);
    heap_ = std::make_unique<TableHeap>(bpm_, !has_existing_data);
    durable_mode_ = true;

    if (has_existing_data) {
        const page_id_t num_pages = owned_disk_->GetNumPages();
        heap_->InitializeFromDisk(num_pages);
    }

    if (has_existing_log) {
        RunRecoveryIfNeeded();
    }
}

void TransactionManager::RunRecoveryIfNeeded() {
    if (!durable_mode_ || log_manager_ == nullptr) {
        return;
    }

    const RecoveryState state =
        RecoveryManager::Recover(owned_disk_.get(), bpm_, heap_.get(), log_manager_.get());

    if (state.next_xid > 1) {
        next_xid_.store(state.next_xid);
    }

    std::lock_guard<std::mutex> lock(tx_mutex_);
    for (const auto& entry : state.transactions) {
        Transaction tx{};
        tx.id = entry.first;
        tx.snapshot_xid = entry.first;
        tx.status = entry.second;
        tx.in_shrinking = entry.second != TxStatus::ACTIVE;
        transactions_[entry.first] = tx;
    }
}

void TransactionManager::FlushRecoveryState() {
    if (!durable_mode_ || bpm_ == nullptr) {
        return;
    }
    bpm_->FlushAllPages();
    if (log_manager_ != nullptr) {
        log_manager_->Flush();
    }
}

TxID TransactionManager::Begin() {
    const TxID xid = next_xid_.fetch_add(1);

    if (durable_mode_ && log_manager_ != nullptr) {
        log_manager_->Append(LogRecordType::BEGIN, LogManager::EncodeBegin(xid));
    }

    std::lock_guard<std::mutex> lock(tx_mutex_);
    Transaction tx{};
    tx.id = xid;
    tx.snapshot_xid = xid;
    tx.status = TxStatus::ACTIVE;
    tx.in_shrinking = false;
    for (const auto& pair : transactions_) {
        if (pair.second.status == TxStatus::ACTIVE) {
            tx.active_txs.insert(pair.first);
        }
    }
    transactions_[xid] = tx;
    return xid;
}

void TransactionManager::Commit(TxID xid) {
    if (durable_mode_ && log_manager_ != nullptr) {
        log_manager_->Append(LogRecordType::COMMIT, LogManager::EncodeCommit(xid));
        log_manager_->Flush();
    }

    {
        std::lock_guard<std::mutex> lock(tx_mutex_);
        GetTransaction(xid).status = TxStatus::COMMITTED;
        GetTransaction(xid).in_shrinking = true;
    }
    lock_manager_.ReleaseLocks(xid);
    GarbageCollect();
}

void TransactionManager::Abort(TxID xid) {
    RollbackVersions(xid);

    if (durable_mode_ && log_manager_ != nullptr) {
        log_manager_->Append(LogRecordType::ABORT, LogManager::EncodeAbort(xid));
        log_manager_->Flush();
    }

    {
        std::lock_guard<std::mutex> lock(tx_mutex_);
        GetTransaction(xid).status = TxStatus::ABORTED;
        GetTransaction(xid).in_shrinking = true;
    }
    lock_manager_.ReleaseLocks(xid);
}

std::optional<std::string> TransactionManager::Read(TxID xid, const RowKey& key) {
    const TxID snapshot_xid = GetSnapshotXid(xid);
    const std::vector<StoredRowVersion> versions = heap_->GetVersions(key);

    for (const StoredRowVersion& version : versions) {
        if (IsVisible(version, snapshot_xid, xid)) {
            return version.value;
        }
    }
    return std::nullopt;
}

void TransactionManager::Insert(TxID xid, const RowKey& key, const std::string& value) {
    (void)InsertReturningLocation(xid, key, value);
}

RowLocation TransactionManager::InsertReturningLocation(TxID xid, const RowKey& key,
                                                        const std::string& value) {
    bool in_shrinking = false;
    {
        std::lock_guard<std::mutex> lock(tx_mutex_);
        in_shrinking = GetTransaction(xid).in_shrinking;
    }
    lock_manager_.AcquireLock(key, xid, LockMode::EXCLUSIVE, in_shrinking);
    return MvccInsert(key, value, xid);
}

void TransactionManager::Update(TxID xid, const RowKey& key, const std::string& value) {
    bool in_shrinking = false;
    {
        std::lock_guard<std::mutex> lock(tx_mutex_);
        in_shrinking = GetTransaction(xid).in_shrinking;
    }
    lock_manager_.AcquireLock(key, xid, LockMode::EXCLUSIVE, in_shrinking);
    MvccUpdate(key, value, xid);
}

void TransactionManager::Remove(TxID xid, const RowKey& key) {
    bool in_shrinking = false;
    {
        std::lock_guard<std::mutex> lock(tx_mutex_);
        in_shrinking = GetTransaction(xid).in_shrinking;
    }
    lock_manager_.AcquireLock(key, xid, LockMode::EXCLUSIVE, in_shrinking);
    MvccDelete(key, xid);
}

bool TransactionManager::IsCommitted(TxID xid) const {
    std::lock_guard<std::mutex> lock(tx_mutex_);
    const auto it = transactions_.find(xid);
    return it != transactions_.end() && it->second.status == TxStatus::COMMITTED;
}

bool TransactionManager::IsAborted(TxID xid) const {
    std::lock_guard<std::mutex> lock(tx_mutex_);
    const auto it = transactions_.find(xid);
    return it != transactions_.end() && it->second.status == TxStatus::ABORTED;
}

TxStatus TransactionManager::GetStatus(TxID xid) const {
    std::lock_guard<std::mutex> lock(tx_mutex_);
    return GetTransaction(xid).status;
}

bool TransactionManager::IsVisible(const StoredRowVersion& version, TxID snapshot_xid,
                                   TxID reader_xid) const {
    bool xmin_visible = false;
    if (version.xmin == reader_xid) {
        xmin_visible = true;
    } else if (IsCommitted(version.xmin) && version.xmin < snapshot_xid) {
        const auto& reader_tx = GetTransaction(reader_xid);
        if (reader_tx.active_txs.find(version.xmin) == reader_tx.active_txs.end()) {
            xmin_visible = true;
        }
    }

    if (!xmin_visible) {
        return false;
    }

    if (version.xmax == INVALID_VERSION_TID) {
        return true;
    }

    bool xmax_visible = false;
    if (version.xmax == reader_xid) {
        xmax_visible = true;
    } else if (IsCommitted(version.xmax) && version.xmax < snapshot_xid) {
        const auto& reader_tx = GetTransaction(reader_xid);
        if (reader_tx.active_txs.find(version.xmax) == reader_tx.active_txs.end()) {
            xmax_visible = true;
        }
    }

    return !xmax_visible;
}

TxID TransactionManager::GetSnapshotXid(TxID xid) const {
    std::lock_guard<std::mutex> lock(tx_mutex_);
    return GetTransaction(xid).snapshot_xid;
}

Transaction& TransactionManager::GetTransaction(TxID xid) {
    const auto it = transactions_.find(xid);
    if (it == transactions_.end()) {
        throw std::runtime_error("Unknown transaction id: " + std::to_string(xid));
    }
    return it->second;
}

const Transaction& TransactionManager::GetTransaction(TxID xid) const {
    const auto it = transactions_.find(xid);
    if (it == transactions_.end()) {
        throw std::runtime_error("Unknown transaction id: " + std::to_string(xid));
    }
    return it->second;
}

void TransactionManager::LogInsertRow(TxID xid, const RowKey& key, const RowLocation& location,
                                      const StoredRowVersion& version) {
    if (!durable_mode_ || log_manager_ == nullptr) {
        return;
    }

    char* data = bpm_->FetchPage(location.page_id);
    Page page(data);
    const SlotEntry slot = page.GetSlot(location.slot_index);
    bpm_->UnpinPage(location.page_id);

    const uint64_t lsn = log_manager_->Append(
        LogRecordType::INSERT_ROW,
        LogManager::EncodeInsertRow(xid, location.page_id, location.slot_index, slot.offset,
                                    slot.length, key, version.xmin, version.xmax,
                                    version.prev_version_tid, version.value));
    heap_->SetPageLsn(location.page_id, lsn);
}

void TransactionManager::LogUpdateXmax(TxID xid, const RowLocation& location, uint64_t old_xmax,
                                       uint64_t new_xmax) {
    if (!durable_mode_ || log_manager_ == nullptr) {
        return;
    }

    const uint64_t lsn = log_manager_->Append(
        LogRecordType::UPDATE_XMAX,
        LogManager::EncodeUpdateXmax(xid, location.page_id, location.slot_index, old_xmax,
                                     new_xmax));
    heap_->SetPageLsn(location.page_id, lsn);
}

RowLocation TransactionManager::MvccInsert(const RowKey& key, const std::string& value,
                                           TxID xid) {
    const RowLocation location = heap_->InsertVersion(key, value, xid);
    const StoredRowVersion version = heap_->ReadVersionAt(location);
    LogInsertRow(xid, key, location, version);
    return location;
}

void TransactionManager::MvccUpdate(const RowKey& key, const std::string& value, TxID xid) {
    const TxID snapshot_xid = GetSnapshotXid(xid);
    const std::vector<StoredRowVersion> versions = heap_->GetVersions(key);

    for (const StoredRowVersion& version : versions) {
        if (IsVisible(version, snapshot_xid, xid) && version.xmax == INVALID_VERSION_TID) {
            const uint64_t old_xmax = heap_->GetVersionXmax(version.location);
            heap_->SetXmax(version.location, xid);
            LogUpdateXmax(xid, version.location, old_xmax, xid);
            break;
        }
    }

    const RowLocation location = heap_->InsertVersion(key, value, xid);
    const StoredRowVersion new_version = heap_->ReadVersionAt(location);
    LogInsertRow(xid, key, location, new_version);
}

void TransactionManager::MvccDelete(const RowKey& key, TxID xid) {
    const TxID snapshot_xid = GetSnapshotXid(xid);
    const std::vector<StoredRowVersion> versions = heap_->GetVersions(key);

    for (const StoredRowVersion& version : versions) {
        if (IsVisible(version, snapshot_xid, xid) && version.xmax == INVALID_VERSION_TID) {
            const uint64_t old_xmax = heap_->GetVersionXmax(version.location);
            heap_->SetXmax(version.location, xid);
            LogUpdateXmax(xid, version.location, old_xmax, xid);
            return;
        }
    }
}

void TransactionManager::RollbackVersions(TxID xid) {
    heap_->RollbackVersions(xid);
}

TxID TransactionManager::ComputeGlobalXmin() const {
    std::lock_guard<std::mutex> lock(tx_mutex_);
    TxID global_xmin = next_xid_.load();
    for (const auto& pair : transactions_) {
        if (pair.second.status == TxStatus::ACTIVE) {
            global_xmin = std::min(global_xmin, pair.second.snapshot_xid);
        }
    }
    return global_xmin;
}

void TransactionManager::GarbageCollect() {
    const TxID global_xmin = ComputeGlobalXmin();
    heap_->PruneDeadVersions([this, global_xmin](const StoredRowVersion& version) {
        if (IsAborted(version.xmin) && version.xmax == version.xmin) {
            return true;
        }
        if (version.xmax != INVALID_VERSION_TID && IsCommitted(version.xmax)) {
            return version.xmax < global_xmin;
        }
        return false;
    });
}

}  // namespace minidb
