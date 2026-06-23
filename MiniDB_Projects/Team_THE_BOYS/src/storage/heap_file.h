#pragma once

#include <vector>

#include "common/types.h"
#include "storage/buffer_pool.h"
#include "storage/page_manager.h"

namespace minidb {

class HeapFile {
public:
    HeapFile(PageManager* page_manager, BufferPool* buffer_pool, int first_page_id);

    int first_page_id() const { return first_page_id_; }
    int last_page_id() const { return last_page_id_; }
    void set_last_page_id(int page_id) { last_page_id_ = page_id; }

    Rid InsertTuple(const Row& row);
    std::optional<Row> GetTuple(const Rid& rid) const;
    bool DeleteTuple(const Rid& rid);
    std::vector<Rid> ScanAll() const;

private:
    PageManager* page_manager_;
    BufferPool* buffer_pool_;
    int first_page_id_;
    int last_page_id_;

    std::optional<Rid> InsertInPage(int page_id, const Row& row);
    int AllocatePage();
};

}  // namespace minidb
