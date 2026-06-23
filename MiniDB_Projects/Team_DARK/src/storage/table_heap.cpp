#include "storage/table_heap.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace minidb {

namespace {

constexpr char kKeyPayloadSeparator = '\t';
constexpr page_id_t kCatalogMetaPageStart = 10000;

bool IsPlausibleHeapPage(const PageHeader& header) {
    return header.free_space_pointer <= PAGE_SIZE;
}

bool IsPlausibleSlot(const SlotEntry& slot) {
    if (slot.length < ROW_VERSION_HEADER_SIZE) {
        return false;
    }
    const std::size_t row_end = static_cast<std::size_t>(slot.offset) + slot.length;
    return slot.offset >= PAGE_HEADER_SIZE && row_end <= PAGE_SIZE;
}

std::string EncodeStoredPayload(const std::string& key, const std::string& value) {
    return key + kKeyPayloadSeparator + value;
}

bool TryDecodeStoredPayload(const std::string& stored, std::string* key, std::string* value) {
    const std::size_t sep = stored.find(kKeyPayloadSeparator);
    if (sep == std::string::npos) {
        return false;
    }
    *key = stored.substr(0, sep);
    *value = stored.substr(sep + 1);
    return true;
}

std::string DecodeValueFromStored(const std::string& stored) {
    std::string key;
    std::string value;
    if (TryDecodeStoredPayload(stored, &key, &value)) {
        return value;
    }
    return stored;
}

}  // namespace

TableHeap::TableHeap(BufferPoolManager* bpm, bool create_initial_page)
    : bpm_(bpm), next_page_id_(0), insert_page_id_(INVALID_PAGE_ID) {
    if (bpm_ == nullptr) {
        throw std::invalid_argument("bpm must not be null");
    }

    if (!create_initial_page) {
        return;
    }

    const page_id_t first_page = next_page_id_++;
    char* data = bpm_->FetchPage(first_page);
    Page::InitPage(data);
    bpm_->MarkDirty(first_page);
    bpm_->UnpinPage(first_page);
    insert_page_id_ = first_page;
}

void TableHeap::InitializeFromDisk(page_id_t num_pages) {
    std::lock_guard<std::mutex> lock(mu_);
    const page_id_t scan_limit = std::min(num_pages, kCatalogMetaPageStart);
    page_id_t max_heap_page = INVALID_PAGE_ID;
    key_versions_.clear();

    for (page_id_t page_id = 0; page_id < scan_limit; ++page_id) {
        char* data = bpm_->FetchPage(page_id);
        Page page(data);
        const PageHeader header = page.GetHeader();
        if (!IsPlausibleHeapPage(header)) {
            bpm_->UnpinPage(page_id);
            continue;
        }

        bool page_has_rows = false;
        for (uint32_t slot_idx = 0; slot_idx < header.slot_count; ++slot_idx) {
            const SlotEntry slot = page.GetSlot(slot_idx);
            if (!IsPlausibleSlot(slot)) {
                continue;
            }

            const std::size_t payload_size = slot.length - ROW_VERSION_HEADER_SIZE;
            std::string stored(payload_size, '\0');
            std::memcpy(stored.data(), data + slot.offset + ROW_VERSION_HEADER_SIZE, payload_size);

            std::string key;
            std::string value;
            if (!TryDecodeStoredPayload(stored, &key, &value)) {
                continue;
            }

            page_has_rows = true;
            const RowLocation location{page_id, static_cast<uint16_t>(slot_idx)};
            auto& versions = key_versions_[key];
            if (std::find(versions.begin(), versions.end(), location) == versions.end()) {
                versions.insert(versions.begin(), location);
            }
        }

        if (page_has_rows || header.slot_count > 0) {
            max_heap_page = page_id;
        }
        bpm_->UnpinPage(page_id);
    }

    if (max_heap_page != INVALID_PAGE_ID) {
        next_page_id_ = max_heap_page + 1;
        insert_page_id_ = max_heap_page;
    } else {
        next_page_id_ = 0;
        insert_page_id_ = INVALID_PAGE_ID;
    }
}

bool TableHeap::PageHasSpace(const PageHeader& header, std::size_t row_size) const {
    const std::size_t slot_array_end =
        PAGE_HEADER_SIZE + static_cast<std::size_t>(header.slot_count) * SLOT_ENTRY_SIZE;
    const std::size_t new_free_ptr = static_cast<std::size_t>(header.free_space_pointer) - row_size;
    return new_free_ptr >= slot_array_end + SLOT_ENTRY_SIZE;
}

RowLocation TableHeap::InsertRow(page_id_t page_id, const RowVersionHeader& header,
                                 const std::string& value) {
    const std::size_t row_size = ROW_VERSION_HEADER_SIZE + value.size();
    if (row_size > PAGE_SIZE - PAGE_HEADER_SIZE - SLOT_ENTRY_SIZE) {
        throw std::runtime_error("row too large for page");
    }

    char* data = bpm_->FetchPage(page_id);
    Page page(data);
    PageHeader page_header = page.GetHeader();

    if (!PageHasSpace(page_header, row_size)) {
        bpm_->UnpinPage(page_id);
        throw std::runtime_error("page has no space");
    }

    const std::size_t row_offset =
        static_cast<std::size_t>(page_header.free_space_pointer) - row_size;
    page.SetRowVersionHeader(row_offset, header);
    if (!value.empty()) {
        std::memcpy(data + row_offset + ROW_VERSION_HEADER_SIZE, value.data(), value.size());
    }

    const SlotEntry slot{static_cast<uint16_t>(row_offset), static_cast<uint16_t>(row_size)};
    page.SetSlot(page_header.slot_count, slot);
    ++page_header.slot_count;
    page_header.free_space_pointer = static_cast<uint32_t>(row_offset);
    page.SetHeader(page_header);

    bpm_->MarkDirty(page_id);
    bpm_->UnpinPage(page_id);

    return RowLocation{page_id, static_cast<uint16_t>(page_header.slot_count - 1)};
}

page_id_t TableHeap::EnsureInsertPage(std::size_t row_size) {
    if (insert_page_id_ != INVALID_PAGE_ID) {
        char* data = bpm_->FetchPage(insert_page_id_);
        Page page(data);
        const PageHeader header = page.GetHeader();
        bpm_->UnpinPage(insert_page_id_);
        if (PageHasSpace(header, row_size)) {
            return insert_page_id_;
        }
    }

    const page_id_t new_page = next_page_id_++;
    char* data = bpm_->FetchPage(new_page);
    Page::InitPage(data);
    bpm_->MarkDirty(new_page);
    bpm_->UnpinPage(new_page);
    insert_page_id_ = new_page;
    return new_page;
}

RowLocation TableHeap::AllocateRow(const RowVersionHeader& header, const std::string& value) {
    const std::size_t row_size = ROW_VERSION_HEADER_SIZE + value.size();
    const page_id_t page_id = EnsureInsertPage(row_size);
    return InsertRow(page_id, header, value);
}

RowLocation TableHeap::InsertVersion(const std::string& key, const std::string& value,
                                     uint64_t xmin, uint64_t xmax,
                                     uint64_t prev_version_tid) {
    std::lock_guard<std::mutex> lock(mu_);

    RowVersionHeader header{};
    header.xmin = xmin;
    header.xmax = xmax;
    header.prev_version_tid = prev_version_tid;

    const std::string stored_payload = EncodeStoredPayload(key, value);
    const RowLocation location = AllocateRow(header, stored_payload);
    auto& versions = key_versions_[key];
    versions.insert(versions.begin(), location);
    return location;
}

StoredRowVersion TableHeap::ReadVersion(const RowLocation& location) const {
    char* data = bpm_->FetchPage(location.page_id);
    Page page(data);
    const SlotEntry slot = page.GetSlot(location.slot_index);
    const RowVersionHeader header = page.GetRowVersionHeader(slot.offset);

    StoredRowVersion version{};
    version.xmin = header.xmin;
    version.xmax = header.xmax;
    version.prev_version_tid = header.prev_version_tid;
    version.location = location;

    const std::size_t value_size = slot.length - ROW_VERSION_HEADER_SIZE;
    if (value_size > 0) {
        version.value.resize(value_size);
        std::memcpy(version.value.data(), data + slot.offset + ROW_VERSION_HEADER_SIZE,
                    value_size);
        version.value = DecodeValueFromStored(version.value);
    }

    bpm_->UnpinPage(location.page_id);
    return version;
}

std::vector<StoredRowVersion> TableHeap::GetVersions(const std::string& key) const {
    std::lock_guard<std::mutex> lock(mu_);

    std::vector<StoredRowVersion> versions;
    const auto it = key_versions_.find(key);
    if (it == key_versions_.end()) {
        return versions;
    }

    versions.reserve(it->second.size());
    for (const RowLocation& location : it->second) {
        versions.push_back(ReadVersion(location));
    }
    return versions;
}

std::vector<std::string> TableHeap::ListKeys() const {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<std::string> keys;
    keys.reserve(key_versions_.size());
    for (const auto& entry : key_versions_) {
        keys.push_back(entry.first);
    }
    return keys;
}

StoredRowVersion TableHeap::ReadVersionAt(const RowLocation& location) const {
    std::lock_guard<std::mutex> lock(mu_);
    return ReadVersion(location);
}

void TableHeap::WriteVersionHeader(const RowLocation& location,
                                   const RowVersionHeader& header) {
    char* data = bpm_->FetchPage(location.page_id);
    Page page(data);
    page.SetRowVersionHeader(page.GetSlot(location.slot_index).offset, header);
    bpm_->MarkDirty(location.page_id);
    bpm_->UnpinPage(location.page_id);
}

void TableHeap::SetXmax(const RowLocation& location, uint64_t xmax) {
    std::lock_guard<std::mutex> lock(mu_);

    char* data = bpm_->FetchPage(location.page_id);
    Page page(data);
    const SlotEntry slot = page.GetSlot(location.slot_index);
    RowVersionHeader header = page.GetRowVersionHeader(slot.offset);
    header.xmax = xmax;
    page.SetRowVersionHeader(slot.offset, header);
    bpm_->MarkDirty(location.page_id);
    bpm_->UnpinPage(location.page_id);
}

std::size_t TableHeap::PruneDeadVersions(
    const std::function<bool(const StoredRowVersion&)>& is_dead) {
    std::lock_guard<std::mutex> lock(mu_);
    std::size_t pruned = 0;

    for (auto it = key_versions_.begin(); it != key_versions_.end();) {
        auto& locations = it->second;
        locations.erase(
            std::remove_if(locations.begin(), locations.end(),
                           [&](const RowLocation& location) {
                               const StoredRowVersion version = ReadVersion(location);
                               if (is_dead(version)) {
                                   ++pruned;
                                   return true;
                               }
                               return false;
                           }),
            locations.end());

        if (locations.empty()) {
            it = key_versions_.erase(it);
        } else {
            ++it;
        }
    }

    return pruned;
}

void TableHeap::RollbackVersions(uint64_t xid) {
    std::lock_guard<std::mutex> lock(mu_);

    for (const auto& entry : key_versions_) {
        for (const RowLocation& location : entry.second) {
            char* data = bpm_->FetchPage(location.page_id);
            Page page(data);
            const SlotEntry slot = page.GetSlot(location.slot_index);
            RowVersionHeader header = page.GetRowVersionHeader(slot.offset);

            bool changed = false;
            if (header.xmin == xid) {
                header.xmax = xid;
                changed = true;
            }
            if (header.xmax == xid) {
                header.xmax = INVALID_VERSION_TID;
                changed = true;
            }

            if (changed) {
                page.SetRowVersionHeader(slot.offset, header);
                bpm_->MarkDirty(location.page_id);
            }
            bpm_->UnpinPage(location.page_id);
        }
    }
}

void TableHeap::SetMetadata(page_id_t next_page_id, page_id_t insert_page_id) {
    std::lock_guard<std::mutex> lock(mu_);
    next_page_id_ = next_page_id;
    insert_page_id_ = insert_page_id;
}

uint64_t TableHeap::GetVersionXmax(const RowLocation& location) const {
    std::lock_guard<std::mutex> lock(mu_);

    char* data = bpm_->FetchPage(location.page_id);
    Page page(data);
    const SlotEntry slot = page.GetSlot(location.slot_index);
    const RowVersionHeader header = page.GetRowVersionHeader(slot.offset);
    bpm_->UnpinPage(location.page_id);
    return header.xmax;
}

void TableHeap::SetPageLsn(page_id_t page_id, uint64_t lsn) {
    std::lock_guard<std::mutex> lock(mu_);

    char* data = bpm_->FetchPage(page_id);
    Page page(data);
    PageHeader header = page.GetHeader();
    header.lsn = lsn;
    page.SetHeader(header);
    bpm_->MarkDirty(page_id);
    bpm_->UnpinPage(page_id);
}

void TableHeap::RecoveryRegisterKey(const std::string& key, const RowLocation& location) {
    std::lock_guard<std::mutex> lock(mu_);
    auto& versions = key_versions_[key];
  if (std::find(versions.begin(), versions.end(), location) == versions.end()) {
        versions.insert(versions.begin(), location);
    }
}

void TableHeap::RecoveryRedoInsert(const RowLocation& location, uint16_t row_offset,
                                   uint16_t row_length, const std::string& key,
                                   const RowVersionHeader& header, const std::string& value,
                                   uint64_t lsn) {
    (void)row_length;
    std::lock_guard<std::mutex> lock(mu_);

    char* data = bpm_->FetchPage(location.page_id);
    Page page(data);
    PageHeader page_header = page.GetHeader();
    if (page_header.free_space_pointer < PAGE_HEADER_SIZE ||
        page_header.free_space_pointer > PAGE_SIZE) {
        Page::InitPage(data);
        page_header = page.GetHeader();
    }

    page.SetRowVersionHeader(row_offset, header);
    const std::string stored_payload = EncodeStoredPayload(key, value);
    if (!stored_payload.empty()) {
        std::memcpy(data + row_offset + ROW_VERSION_HEADER_SIZE, stored_payload.data(),
                    stored_payload.size());
    }

    const SlotEntry slot{row_offset, static_cast<uint16_t>(ROW_VERSION_HEADER_SIZE + stored_payload.size())};
    while (page_header.slot_count <= location.slot_index) {
        const SlotEntry empty_slot{0, 0};
        page.SetSlot(page_header.slot_count, empty_slot);
        ++page_header.slot_count;
    }
    page.SetSlot(location.slot_index, slot);
    if (location.slot_index + 1 > page_header.slot_count) {
        page_header.slot_count = location.slot_index + 1;
    }
    if (row_offset < page_header.free_space_pointer) {
        page_header.free_space_pointer = row_offset;
    }
    page_header.lsn = lsn;
    page.SetHeader(page_header);

    bpm_->MarkDirty(location.page_id);
    bpm_->UnpinPage(location.page_id);

    auto& versions = key_versions_[key];
    if (std::find(versions.begin(), versions.end(), location) == versions.end()) {
        versions.insert(versions.begin(), location);
    }
}

void TableHeap::RecoveryRedoSetXmax(const RowLocation& location, uint64_t new_xmax,
                                    uint64_t lsn) {
    std::lock_guard<std::mutex> lock(mu_);

    char* data = bpm_->FetchPage(location.page_id);
    Page page(data);
    const SlotEntry slot = page.GetSlot(location.slot_index);
    RowVersionHeader header = page.GetRowVersionHeader(slot.offset);
    header.xmax = new_xmax;
    page.SetRowVersionHeader(slot.offset, header);

    PageHeader page_header = page.GetHeader();
    page_header.lsn = lsn;
    page.SetHeader(page_header);

    bpm_->MarkDirty(location.page_id);
    bpm_->UnpinPage(location.page_id);
}

void TableHeap::RecoveryUndoInsert(const RowLocation& location, uint64_t tx_id,
                                   const std::string& key) {
    std::lock_guard<std::mutex> lock(mu_);

    char* data = bpm_->FetchPage(location.page_id);
    Page page(data);
    const SlotEntry slot = page.GetSlot(location.slot_index);
    RowVersionHeader header = page.GetRowVersionHeader(slot.offset);
    if (header.xmin == tx_id) {
        header.xmax = tx_id;
        page.SetRowVersionHeader(slot.offset, header);
        bpm_->MarkDirty(location.page_id);
    }
    bpm_->UnpinPage(location.page_id);

    auto key_it = key_versions_.find(key);
    if (key_it != key_versions_.end()) {
        auto& versions = key_it->second;
        versions.erase(
            std::remove(versions.begin(), versions.end(), location),
            versions.end());
        if (versions.empty()) {
            key_versions_.erase(key_it);
        }
    }
}

void TableHeap::RecoveryUndoSetXmax(const RowLocation& location, uint64_t old_xmax) {
    std::lock_guard<std::mutex> lock(mu_);

    char* data = bpm_->FetchPage(location.page_id);
    Page page(data);
    const SlotEntry slot = page.GetSlot(location.slot_index);
    RowVersionHeader header = page.GetRowVersionHeader(slot.offset);
    header.xmax = old_xmax;
    page.SetRowVersionHeader(slot.offset, header);
    bpm_->MarkDirty(location.page_id);
    bpm_->UnpinPage(location.page_id);
}

}  // namespace minidb
