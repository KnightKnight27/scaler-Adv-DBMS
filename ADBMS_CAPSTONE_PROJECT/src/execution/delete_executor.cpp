#include "execution/delete_executor.h"

namespace minidb {

DeleteExecutor::DeleteExecutor(std::unique_ptr<Executor> child_executor, BPlusTree* tree, LogManager* log_manager,
                               BufferPool* buffer_pool, DiskManager* disk_manager)
    : child_executor_(std::move(child_executor)),
      tree_(tree),
      log_manager_(log_manager),
      buffer_pool_(buffer_pool),
      disk_manager_(disk_manager) {}

void DeleteExecutor::Init() {
    child_executor_->Init();
}

bool DeleteExecutor::Next(Tuple* tuple) {
    // Delete the next tuple produced by the child
    if (child_executor_->Next(tuple)) {
        // Fake transaction ID = 2 for capstone demo
        log_manager_->AppendLogRecord(LogRecord(2, LogRecordType::BEGIN));
        
        log_manager_->AppendLogRecord(LogRecord(2, LogRecordType::DELETE, *tuple));
        tree_->Remove(tuple->id);

        int total_pages = disk_manager_->GetFileSize() / PAGE_SIZE;
        for (int page_id = 0; page_id < total_pages; page_id++) {
            Page* page = buffer_pool_->FetchPage(page_id);
            if (page == nullptr) {
                continue;
            }

            auto* heap_page = reinterpret_cast<HeapPage*>(page->data_);
            if (heap_page->magic_ != HEAP_MAGIC) {
                buffer_pool_->UnpinPage(page_id, false);
                continue;
            }

            bool deleted = false;
            Tuple* tuples = heap_page->GetTuples();
            for (int slot = 0; slot < heap_page->num_tuples_; slot++) {
                if (tuples[slot].id == tuple->id && tuples[slot].name[0] != '\0') {
                    tuples[slot] = Tuple();
                    deleted = true;
                    break;
                }
            }

            buffer_pool_->UnpinPage(page_id, deleted);
            if (deleted) {
                break;
            }
        }
        
        log_manager_->AppendLogRecord(LogRecord(2, LogRecordType::COMMIT));
        return true;
    }
    return false;
}

} // namespace minidb
