#pragma once

#include "storage/buffer_pool_manager.h"
#include "storage/page.h"

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace minidb {

struct RowLocation {
    page_id_t page_id = INVALID_PAGE_ID;
    uint16_t slot_index = 0;

    bool operator==(const RowLocation& other) const {
        return page_id == other.page_id && slot_index == other.slot_index;
    }
};

struct StoredRowVersion {
    std::string value;
    uint64_t xmin = 0;
    uint64_t xmax = 0;
    uint64_t prev_version_tid = INVALID_VERSION_TID;
    RowLocation location{};
};

class TableHeap {
public:
    explicit TableHeap(BufferPoolManager* bpm, bool create_initial_page = true);

    TableHeap(const TableHeap&) = delete;
    TableHeap& operator=(const TableHeap&) = delete;

    RowLocation InsertVersion(const std::string& key, const std::string& value, uint64_t xmin,
                              uint64_t xmax = INVALID_VERSION_TID,
                              uint64_t prev_version_tid = INVALID_VERSION_TID);

    std::vector<StoredRowVersion> GetVersions(const std::string& key) const;

    std::vector<std::string> ListKeys() const;

    StoredRowVersion ReadVersionAt(const RowLocation& location) const;

    void SetXmax(const RowLocation& location, uint64_t xmax);

    void RollbackVersions(uint64_t xid);

    std::size_t PruneDeadVersions(
        const std::function<bool(const StoredRowVersion&)>& is_dead);

    void SetMetadata(page_id_t next_page_id, page_id_t insert_page_id);
    void InitializeFromDisk(page_id_t num_pages);
    uint64_t GetVersionXmax(const RowLocation& location) const;
    void SetPageLsn(page_id_t page_id, uint64_t lsn);

    void RecoveryRedoInsert(const RowLocation& location, uint16_t row_offset, uint16_t row_length,
                            const std::string& key, const RowVersionHeader& header,
                            const std::string& value, uint64_t lsn);
    void RecoveryRedoSetXmax(const RowLocation& location, uint64_t new_xmax, uint64_t lsn);
    void RecoveryUndoInsert(const RowLocation& location, uint64_t tx_id, const std::string& key);
    void RecoveryUndoSetXmax(const RowLocation& location, uint64_t old_xmax);
    void RecoveryRegisterKey(const std::string& key, const RowLocation& location);

private:
    bool PageHasSpace(const PageHeader& header, std::size_t row_size) const;
    RowLocation InsertRow(page_id_t page_id, const RowVersionHeader& header,
                          const std::string& value);
    RowLocation AllocateRow(const RowVersionHeader& header, const std::string& value);
    StoredRowVersion ReadVersion(const RowLocation& location) const;
    void WriteVersionHeader(const RowLocation& location, const RowVersionHeader& header);
    page_id_t EnsureInsertPage(std::size_t row_size);

    BufferPoolManager* bpm_;
    page_id_t next_page_id_;
    page_id_t insert_page_id_;
    mutable std::mutex mu_;
    std::unordered_map<std::string, std::vector<RowLocation>> key_versions_;
};

}  // namespace minidb
