#include "storage/HeapFile.hpp"

#include <sstream>

using namespace std;

namespace minidb {

HeapFile::HeapFile(PageManager& page_manager, uint32_t first_page_id)
    : page_manager_(page_manager), first_page_id_(first_page_id) {
    page_ids_.push_back(first_page_id);
}

vector<char> HeapFile::serializeRow(const Row& row) {
    ostringstream oss;
    bool first = true;
    for (const auto& kv : row) {
        if (!first) oss << "|";
        oss << kv.first << "=" << kv.second;
        first = false;
    }
    string s = oss.str();
    return vector<char>(s.begin(), s.end());
}

Row HeapFile::deserializeRow(const vector<char>& bytes) {
    Row row;
    string s(bytes.begin(), bytes.end());
    istringstream iss(s);
    string token;
    while (getline(iss, token, '|')) {
        auto eq = token.find('=');
        if (eq != string::npos) {
            row[token.substr(0, eq)] = token.substr(eq + 1);
        }
    }
    return row;
}

uint32_t HeapFile::findPageWithSpace(const vector<char>& bytes) {
    uint32_t current = first_page_id_;
    Page* page = page_manager_.fetchPage(current);
    if (page == nullptr) return first_page_id_;

    Page trial = *page;
    if (trial.insertRecord(bytes)) return current;

    uint32_t new_id = page_manager_.allocatePage(PageType::HEAP);
    page_ids_.push_back(new_id);
    return new_id;
}

RowLocation HeapFile::insertRow(const Row& row) {
    auto bytes = serializeRow(row);
    uint32_t page_id = findPageWithSpace(bytes);
    Page* page = page_manager_.fetchPage(page_id);
    RowLocation loc{page_id, page->getRecordCount()};
    page->insertRecord(bytes);
    page_manager_.markDirty(page_id);
    return loc;
}

RowList HeapFile::scanAll() const {
    RowList result;
    for (uint32_t page_id : page_ids_) {
        Page* page = page_manager_.fetchPage(page_id);
        if (page == nullptr) continue;

        uint32_t count = page->getRecordCount();
        for (uint32_t i = 0; i < count; ++i) {
            if (page->isSlotDeleted(i)) continue;
            vector<char> bytes;
            if (page->getRecord(i, bytes)) {
                result.push_back(deserializeRow(bytes));
            }
        }
    }
    return result;
}

bool HeapFile::deleteRow(const RowLocation& loc) {
    Page* page = page_manager_.fetchPage(loc.page_id);
    if (page == nullptr) return false;
    bool ok = page->deleteRecord(loc.slot_index);
    if (ok) page_manager_.markDirty(loc.page_id);
    return ok;
}

size_t HeapFile::estimateRowCount() const {
    size_t n = 0;
    for (uint32_t page_id : page_ids_) {
        Page* page = page_manager_.fetchPage(page_id);
        if (page == nullptr) continue;
        for (uint32_t i = 0; i < page->getRecordCount(); ++i) {
            if (!page->isSlotDeleted(i)) ++n;
        }
    }
    return n;
}

}  // namespace minidb
