#include "exec/index_scan.h"
#include "storage/page.h"

namespace minidb {

IndexScan::IndexScan(BPlusTree* tree, HeapFile* heap_file, const Schema& schema, int search_key)
    : tree_(tree), heap_file_(heap_file), schema_(schema), search_key_(search_key), done_(false) {}

void IndexScan::Open() {
    done_ = false;
}

bool IndexScan::Next(Tuple& out) {
    if (done_) return false;
    
    RecordId rid = tree_->search(search_key_);
    if (!rid.is_valid()) {
        done_ = true;
        return false;
    }
    
    char buffer[PAGE_SIZE];
    uint16_t length = 0;
    if (heap_file_->get_tuple(rid, buffer, &length)) {
        out = deserialize_tuple(buffer, length, schema_);
        out.rid = rid;
        done_ = true; // Point query only
        return true;
    }
    
    done_ = true;
    return false;
}

void IndexScan::Close() {}

} // namespace minidb
