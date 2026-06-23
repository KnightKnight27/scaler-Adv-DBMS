#include "test_support.h"
#include "catalog/catalog.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"

using namespace minidb;

void TestStorageAndIndex() {
    const std::string file = "test_storage.db";
    RemoveFile(file);
    Schema schema{{"id", ColumnType::INT}, {"text", ColumnType::STRING}};

    {
        DiskManager disk(file);
        BufferPoolManager pool(8, &disk);
        Catalog catalog;
        TableInfo* table = catalog.CreateTable("items", schema, &pool);
        for (int64_t id = 0; id < 500; ++id) {
            Row row{id, std::string(40, 'a')};
            RecordId rid = table->heap_file->InsertTuple(row);
            Check(rid.page_id != INVALID_PAGE_ID, "multi-page insert failed");
            table->primary_index->Insert(id, rid);
        }
        pool.FlushAll();
        Check(disk.GetNumPages() > 1, "heap file did not allocate multiple pages");
        Check(table->primary_index->Search(250).has_value(), "index search failed");
        Check(table->primary_index->RangeSearch(100, 109).size() == 10,
              "index range search failed");
        Check(table->primary_index->Delete(250), "index delete failed");
    }

    {
        DiskManager disk(file);
        BufferPoolManager pool(8, &disk);
        Catalog catalog;
        TableInfo* table = catalog.CreateTable("items", schema, &pool);
        Check(table->GetTupleCount() == 500, "tuple count did not survive restart");
        Check(table->primary_index->Search(499).has_value(),
              "index was not rebuilt after restart");
    }
    RemoveFile(file);
}
