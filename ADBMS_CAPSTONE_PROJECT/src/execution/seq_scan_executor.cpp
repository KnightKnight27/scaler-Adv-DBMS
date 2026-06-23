#include "execution/seq_scan_executor.h"

namespace minidb {

SeqScanExecutor::SeqScanExecutor(BufferPool* buffer_pool, DiskManager* disk_manager)
    : buffer_pool_(buffer_pool), disk_manager_(disk_manager) {}

void SeqScanExecutor::Init() {
    current_page_id_ = 0;
    current_slot_ = 0;
    total_pages_ = disk_manager_->GetFileSize() / PAGE_SIZE;
}

bool SeqScanExecutor::Next(Tuple* tuple) {
    while (current_page_id_ < total_pages_) {
        Page* page = buffer_pool_->FetchPage(current_page_id_);
        if (page) {
            auto* heap_page = reinterpret_cast<HeapPage*>(page->data_);
            if (heap_page->magic_ == HEAP_MAGIC && current_slot_ < heap_page->num_tuples_) {
                Tuple* t = &heap_page->GetTuples()[current_slot_];
                current_slot_++;
                buffer_pool_->UnpinPage(current_page_id_, false);
                
                // Assume tuple with id 0 and empty name is a tombstone for seq scan
                if (t->id != 0 || t->name[0] != '\0') {
                    *tuple = *t;
                    return true;
                }
                continue;
            }
            buffer_pool_->UnpinPage(current_page_id_, false);
        }
        current_page_id_++;
        current_slot_ = 0;
    }
    return false;
}

} // namespace minidb
