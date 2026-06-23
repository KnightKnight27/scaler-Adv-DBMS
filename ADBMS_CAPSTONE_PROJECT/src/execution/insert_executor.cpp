#include "execution/insert_executor.h"
#include <iostream>

namespace minidb {

InsertExecutor::InsertExecutor(BPlusTree* tree, LogManager* log_manager, TableStats* stats, BufferPool* buffer_pool, DiskManager* disk_manager, int32_t id, const std::string& name)
    : tree_(tree), log_manager_(log_manager), stats_(stats), buffer_pool_(buffer_pool), disk_manager_(disk_manager), id_(id), name_(name) {}

void InsertExecutor::Init() {
    inserted_ = false;
}

bool InsertExecutor::Next(Tuple* tuple) {
    if (inserted_) return false;

    Tuple new_tuple(id_, name_.c_str());
    *tuple = new_tuple;

    RecordId existing;
    if (tree_->Search(id_, &existing)) {
        inserted_ = true;
        return false;
    }

    log_manager_->AppendLogRecord(LogRecord(1, LogRecordType::BEGIN));

    // Find a data page with space
    PageId page_id = 0;
    int32_t slot_num = -1;
    Page* page = nullptr;
    HeapPage* heap_page = nullptr;
    
    int total_pages = disk_manager_->GetFileSize() / PAGE_SIZE;
    for (int i = 0; i < total_pages; i++) {
        page = buffer_pool_->FetchPage(i);
        if (page) {
            heap_page = reinterpret_cast<HeapPage*>(page->data_);
            if (heap_page->magic_ == HEAP_MAGIC && heap_page->num_tuples_ < static_cast<int32_t>((PAGE_SIZE - sizeof(HeapPage)) / sizeof(Tuple))) {
                page_id = i;
                break;
            }
            buffer_pool_->UnpinPage(i, false);
        }
        page = nullptr;
        heap_page = nullptr;
    }
    
    if (!page) {
        page = buffer_pool_->NewPage(&page_id);
        heap_page = reinterpret_cast<HeapPage*>(page->data_);
        heap_page->Init();
    }
    
    if (page && heap_page) {
        slot_num = heap_page->num_tuples_;
        heap_page->InsertTuple(new_tuple);
        buffer_pool_->UnpinPage(page_id, true);
        std::cout << "[Diagnostic] INSERT write to Page " << page_id << "\n";
    } else {
        // Fallback fake rid if out of pages
        slot_num = 0; 
    }

    RecordId rid{page_id, slot_num};

    log_manager_->AppendLogRecord(LogRecord(1, LogRecordType::INSERT, new_tuple));
    if (!tree_->Insert(id_, rid)) {
        return false;
    }
    if (stats_) {
        stats_->Update(id_);
    }

    log_manager_->AppendLogRecord(LogRecord(1, LogRecordType::COMMIT));

    inserted_ = true;
    return true; 
}

} // namespace minidb
