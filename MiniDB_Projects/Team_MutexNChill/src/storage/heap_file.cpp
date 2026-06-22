#include "heap_file.h"

HeapFile::HeapFile(const std::string& filename)
    : pm(filename), bp(pm, 10) {
    // Make sure at least one page exists so we always have somewhere to write.
    if (pm.pageCount() == 0) {
        pm.allocatePage();
    }
}

HeapFile::~HeapFile() {
    bp.flushAll();
}

RID HeapFile::insertRow(const Row& row) {
    // Walk every existing page and insert into the first one with a free slot.
    for (int pid = 0; pid < pm.pageCount(); pid++) {
        Page* page = bp.fetchPage(pid);
        if (page->header().num_rows < ROWS_PER_PAGE) {
            int slot = page->header().num_rows;
            page->rows()[slot]          = row;
            page->rows()[slot].is_valid = true;
            page->header().num_rows++;
            bp.unpinPage(pid, /*dirty=*/true);
            return {pid, slot};
        }
        bp.unpinPage(pid, false);
    }

    // All existing pages are full — allocate a new one.
    int pid   = pm.allocatePage();
    Page* page = bp.fetchPage(pid);
    page->rows()[0]          = row;
    page->rows()[0].is_valid = true;
    page->header().num_rows  = 1;
    bp.unpinPage(pid, true);
    return {pid, 0};
}

void HeapFile::deleteRow(const RID& rid) {
    Page* page = bp.fetchPage(rid.page_id);
    page->rows()[rid.slot].is_valid = false;
    bp.unpinPage(rid.page_id, true);
}

Row HeapFile::getRow(const RID& rid) {
    Page* page = bp.fetchPage(rid.page_id);
    Row r = page->rows()[rid.slot];
    bp.unpinPage(rid.page_id, false);
    return r;
}

std::vector<Row> HeapFile::scanAll() {
    std::vector<Row> result;
    for (int pid = 0; pid < pm.pageCount(); pid++) {
        Page* page = bp.fetchPage(pid);
        int n = page->header().num_rows;
        for (int s = 0; s < n; s++) {
            if (page->rows()[s].is_valid) {
                result.push_back(page->rows()[s]);
            }
        }
        bp.unpinPage(pid, false);
    }
    return result;
}

void HeapFile::flush() {
    bp.flushAll();
}
