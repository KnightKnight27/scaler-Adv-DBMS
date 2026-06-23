#include "storage/heap_file.h"

#include <sstream>

namespace minidb {

HeapFile::HeapFile(const std::string& table_name, BufferPoolManager* bpm,
                   const Schema& schema)
    : table_name_(table_name), bpm_(bpm), schema_(schema) {
    if (bpm_->GetDiskManager()->GetNumPages() > 0) {
        first_page_id_ = 0;
        ScanAll([&](const RecordId&, const Row&) { tuple_count_++; });
        page_id_t current = first_page_id_;
        while (current != INVALID_PAGE_ID) {
            Page* page = bpm_->FetchPage(current);
            if (!page) break;
            last_page_id_ = current;
            current = page->GetNextPageId();
            bpm_->UnpinPage(last_page_id_, false);
        }
        return;
    }

    Page* page = bpm_->NewPage(first_page_id_);
    if (page) {
        last_page_id_ = first_page_id_;
        bpm_->UnpinPage(first_page_id_, true);
    }
}

std::string HeapFile::SerializeRow(const Row& row, const Schema&) {
    std::ostringstream out;
    for (size_t i = 0; i < row.size(); ++i) {
        if (i) out << '|';
        if (std::holds_alternative<int64_t>(row[i])) out << std::get<int64_t>(row[i]);
        else if (std::holds_alternative<double>(row[i])) out << std::get<double>(row[i]);
        else out << std::get<std::string>(row[i]);
    }
    return out.str();
}

Row HeapFile::DeserializeRow(const char* data, uint16_t len, const Schema& schema) {
    std::istringstream input(std::string(data, len));
    std::string token;
    Row row;
    for (size_t column = 0;
         column < schema.size() && std::getline(input, token, '|'); ++column) {
        switch (schema[column].type) {
            case ColumnType::INT: row.push_back(static_cast<int64_t>(std::stoll(token))); break;
            case ColumnType::DOUBLE: row.push_back(std::stod(token)); break;
            case ColumnType::STRING: row.push_back(token); break;
        }
    }
    return row;
}

RecordId HeapFile::InsertTuple(const Row& row) {
    std::string bytes = SerializeRow(row, schema_);
    page_id_t page_id;
    Page* page = FindOrAllocatePage(static_cast<uint16_t>(bytes.size()), page_id);
    if (!page) return {INVALID_PAGE_ID, 0};
    int slot = page->InsertRecord(bytes.data(), static_cast<uint16_t>(bytes.size()));
    bpm_->UnpinPage(page_id, slot >= 0);
    if (slot < 0) return {INVALID_PAGE_ID, 0};
    tuple_count_++;
    return {page_id, static_cast<slot_id_t>(slot)};
}

bool HeapFile::GetTuple(const RecordId& rid, Row& out) {
    Page* page = bpm_->FetchPage(rid.page_id);
    if (!page) return false;
    std::vector<char> bytes;
    bool found = page->GetRecord(rid.slot_id, bytes);
    bpm_->UnpinPage(rid.page_id, false);
    if (found) out = DeserializeRow(bytes.data(),
        static_cast<uint16_t>(bytes.size()), schema_);
    return found;
}

bool HeapFile::DeleteTuple(const RecordId& rid) {
    Page* page = bpm_->FetchPage(rid.page_id);
    if (!page) return false;
    bool deleted = page->DeleteRecord(rid.slot_id);
    bpm_->UnpinPage(rid.page_id, deleted);
    if (deleted) tuple_count_--;
    return deleted;
}

void HeapFile::ScanAll(
    std::function<void(const RecordId&, const Row&)> callback) {
    page_id_t page_id = first_page_id_;
    while (page_id != INVALID_PAGE_ID) {
        Page* page = bpm_->FetchPage(page_id);
        if (!page) break;
        for (slot_id_t slot = 0; slot < page->GetNumSlots(); ++slot) {
            std::vector<char> bytes;
            if (page->GetRecord(slot, bytes)) {
                callback({page_id, slot}, DeserializeRow(bytes.data(),
                    static_cast<uint16_t>(bytes.size()), schema_));
            }
        }
        page_id_t next = page->GetNextPageId();
        bpm_->UnpinPage(page_id, false);
        page_id = next;
    }
}

Page* HeapFile::FindOrAllocatePage(uint16_t size, page_id_t& page_id) {
    if (last_page_id_ != INVALID_PAGE_ID) {
        Page* last = bpm_->FetchPage(last_page_id_);
        if (last && last->HasSpace(size)) {
            page_id = last_page_id_;
            return last;
        }
        if (last) bpm_->UnpinPage(last_page_id_, false);
    }

    Page* page = bpm_->NewPage(page_id);
    if (!page) return nullptr;
    if (first_page_id_ == INVALID_PAGE_ID) {
        first_page_id_ = page_id;
    } else {
        Page* last = bpm_->FetchPage(last_page_id_);
        if (!last) {
            bpm_->UnpinPage(page_id, false);
            return nullptr;
        }
        last->SetNextPageId(page_id);
        bpm_->UnpinPage(last_page_id_, true);
    }
    last_page_id_ = page_id;
    return page;
}

} // namespace minidb
