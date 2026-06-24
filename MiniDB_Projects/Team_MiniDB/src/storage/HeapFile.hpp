#pragma once

#include <string>
#include <vector>

using namespace std;

#include "common/Types.hpp"
#include "storage/PageManager.hpp"

namespace minidb {

class HeapFile {
public:
    HeapFile(PageManager& page_manager, uint32_t first_page_id);

    RowLocation insertRow(const Row& row);
    RowList scanAll() const;
    bool deleteRow(const RowLocation& loc);
    uint32_t getFirstPageId() const { return first_page_id_; }
    size_t estimateRowCount() const;

private:
    PageManager& page_manager_;
    uint32_t first_page_id_;
    vector<uint32_t> page_ids_;

    static vector<char> serializeRow(const Row& row);
    static Row deserializeRow(const vector<char>& bytes);
    uint32_t findPageWithSpace(const vector<char>& bytes);
};

}  // namespace minidb
