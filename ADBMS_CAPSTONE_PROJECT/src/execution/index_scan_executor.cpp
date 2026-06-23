#include "execution/index_scan_executor.h"

namespace minidb {

IndexScanExecutor::IndexScanExecutor(BPlusTree* tree, BufferPool* buffer_pool, int32_t search_key)
    : tree_(tree), buffer_pool_(buffer_pool), search_key_(search_key) {}

void IndexScanExecutor::Init() {
    done_ = false;
}

bool IndexScanExecutor::Next(Tuple* tuple) {
    if (done_) {
        return false;
    }

    RecordId rid;
    if (tree_->Search(search_key_, &rid)) {
        Page* page = buffer_pool_->FetchPage(rid.page_id_);
        if (page != nullptr) {
            auto* heap_page = reinterpret_cast<HeapPage*>(page->data_);
            if (heap_page->magic_ == HEAP_MAGIC && rid.slot_num_ < heap_page->num_tuples_) {
                *tuple = heap_page->GetTuples()[rid.slot_num_];
                buffer_pool_->UnpinPage(rid.page_id_, false);
                done_ = true;
                return true;
            }
            buffer_pool_->UnpinPage(rid.page_id_, false);
        }
    }
    
    done_ = true;
    return false;
}

} // namespace minidb
