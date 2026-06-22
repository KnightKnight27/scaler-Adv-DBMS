#include "engine/database.h"

#include "index/bplus_tree.h"
#include "storage/heap_file.h"

namespace minidb {

Database::Database(const std::string& path) {
  dm_.reset(new DiskManager(path));
  pool_.reset(new BufferPool(dm_.get(), BUFFER_POOL_FRAMES));

  if (dm_->NumPages() == 0) {
    // Fresh database: page 0 becomes the catalog meta page.
    PageId meta;
    Frame* f = pool_->NewPage(&meta);  // returns page 0
    catalog_.SerializeTo(f->data);
    pool_->UnpinPage(meta, true);
    pool_->FlushAll();
  } else {
    Frame* f = pool_->FetchPage(0);
    catalog_.DeserializeFrom(f->data);
    pool_->UnpinPage(0, false);
  }
}

Database::~Database() {
  Flush();
}

void Database::SaveCatalog() {
  Frame* f = pool_->FetchPage(0);
  catalog_.SerializeTo(f->data);
  pool_->UnpinPage(0, true);
}

void Database::Flush() {
  SaveCatalog();
  pool_->FlushAll();
}

Status Database::CreateTable(const CreateTableStmt& stmt) {
  if (catalog_.Find(stmt.table) != nullptr) {
    return Status::Error("table already exists: " + stmt.table);
  }

  TableInfo t;
  t.name = stmt.table;
  t.schema.columns = stmt.columns;
  t.schema.pk_index = -1;
  if (!stmt.pk_column.empty()) {
    int idx = t.schema.ColumnIndex(stmt.pk_column);
    if (idx < 0) return Status::Error("PRIMARY KEY column not found: " + stmt.pk_column);
    if (t.schema.columns[idx].type != ValueType::INT) {
      return Status::Error("PRIMARY KEY must be an INT column (MVP limitation)");
    }
    t.schema.pk_index = idx;
  }

  t.heap_first = HeapFile::CreateNew(pool_.get(), t.schema.RecordSize());
  if (t.schema.pk_index >= 0) {
    t.pk_index_header = BPlusTree::CreateNew(pool_.get());
  }
  t.row_count = 0;

  catalog_.tables.push_back(t);
  SaveCatalog();
  pool_->FlushAll();
  return Status::OK();
}

}  // namespace minidb
