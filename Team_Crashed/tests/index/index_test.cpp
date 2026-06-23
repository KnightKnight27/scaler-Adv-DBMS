#include <cassert>
#include <filesystem>
#include <cstdio>

#include "catalog/catalog_manager.h"
#include "catalog/schema.h"
#include "index/index_manager.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"

using namespace minidb;
namespace fs = std::filesystem;

int main() {
    fs::path tmp = fs::temp_directory_path() / "minidb_index_test";
    fs::remove_all(tmp);
    fs::create_directories(tmp);

    storage::DiskManager dm((tmp / "minidb.db").string());
    storage::BufferPool bp(&dm, 8);
    catalog::CatalogManager cat(&dm);
    assert(cat.load() == Status::OK);

    catalog::TableInfo info;
    info.name = "users";
    catalog::Column id;
    id.name = "id";
    id.type = catalog::Type::INT;
    id.isPrimaryKey = true;
    id.nullable = false;
    info.schema.addColumn(id);
    assert(cat.createTable(info) == Status::OK);

    index::IndexManager idx(&bp, &cat);
    assert(idx.createIndex("users", "id", "users_id_pk") == Status::OK);
    assert(idx.findIndex("users", "id") == "users_id_pk");

    index::BPlusTree* tree = idx.open("users_id_pk");
    assert(tree != nullptr);
    assert(tree->insert("k001", 1234) == Status::OK);
    RecordId out = INVALID_RID;
    assert(tree->search("k001", out) == Status::OK);
    assert(out == 1234);
    assert(tree->remove("k001") == Status::OK);
    assert(tree->search("k001", out) == Status::NOT_FOUND);

    std::printf("[OK] index create/search/delete\n");
    return 0;
}
