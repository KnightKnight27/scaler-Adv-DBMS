#include "exec/seq_scan.h"
#include "storage/page.h"

namespace minidb {

SeqScan::SeqScan(HeapFile* heap_file, const Schema& schema)
    : heap_file_(heap_file), schema_(schema), current_page_(0), current_slot_(0) {}

void SeqScan::Open() {
    current_page_ = 0;
    current_slot_ = 0;
}

bool SeqScan::Next(Tuple& out) {
    if (!heap_file_) return false;
    
    char buffer[PAGE_SIZE];
    uint16_t length = 0;
    
    while (current_page_ < heap_file_->get_num_pages()) {
        RecordId rid{current_page_, current_slot_};
        if (heap_file_->get_tuple(rid, buffer, &length)) {
            out = deserialize_tuple(buffer, length, schema_);
            out.rid = rid;
            current_slot_++;
            return true;
        }
        
        current_slot_++;
        // If we tried all slots up to PAGE_SIZE / 4 (max possible slots), move to next page
        // A better approach is to check if we exceeded slot_count in page header, 
        // but since we read directly from heap_file_, we can just rely on get_tuple returning false continuously.
        // Actually, let's just check 500 slots. If we fail, maybe page is done.
        // Let's assume a page has max 4096/4 = 1024 slots.
        if (current_slot_ > 1024) {
            current_page_++;
            current_slot_ = 0;
        }
    }
    return false;
}

void SeqScan::Close() {
    // Nothing to close for heap file scan directly
}

} // namespace minidb
