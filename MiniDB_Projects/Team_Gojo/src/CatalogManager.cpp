#include "CatalogManager.h"

#include <cstdio>
#include <fstream>
#include <iomanip>
#include <memory>
#include <stdexcept>
#include <string>

#include "Optimizer.h"
#include "Table.h"

namespace {

constexpr const char* kCatalogMagic = "MINIDB_CATALOG_V2";

void writeValue(std::ostream& out, const Value& value) {
    if (std::holds_alternative<int>(value)) {
        out << " I " << std::get<int>(value);
    } else {
        out << " S " << std::quoted(std::get<std::string>(value));
    }
}

Value readValue(std::istream& in) {
    char tag = '\0';
    in >> tag;
    if (tag == 'I') {
        int value = 0;
        in >> value;
        return value;
    }
    if (tag == 'S') {
        std::string value;
        in >> std::quoted(value);
        return value;
    }
    throw std::runtime_error("Invalid value tag in catalog");
}

} // namespace

CatalogManager::CatalogManager(const std::string& catalogFile)
    : catalogFile_(catalogFile) {}

void CatalogManager::saveState(const Optimizer& opt) const {
    const std::string tmpFile = catalogFile_ + ".tmp";
    std::ofstream out(tmpFile, std::ios::trunc);
    if (!out) {
        throw std::runtime_error("Could not open catalog for writing: " + tmpFile);
    }

    const auto& tables = opt.getCatalog().getTables();
    out << kCatalogMagic << '\n';
    out << tables.size() << '\n';

    for (const auto& entry : tables) {
        const Table* table = entry.second;
        const Schema& schema = table->getSchema();

        out << std::quoted(table->getName()) << ' '
            << std::quoted(table->getDbFilePath()) << '\n';

        out << schema.getColumns().size() << '\n';
        for (const auto& col : schema.getColumns()) {
            out << std::quoted(col.name) << ' '
                << dataTypeToString(col.type) << ' '
                << col.size << '\n';
        }

        const auto& heap = table->getHeapFile();
        out << table->getNumRows() << ' '
            << table->getIndex()->getRootPageId() << ' '
            << table->getNumDataPages() << ' '
            << heap.size() << '\n';

        for (const auto& record : heap) {
            out << record._record_id << ' '
                << (record.isDeleted() ? 1 : 0) << ' '
                << record.values.size();
            for (const auto& value : record.values) {
                writeValue(out, value);
            }
            out << '\n';
        }
    }

    out.close();
    if (!out) {
        throw std::runtime_error("Failed while writing catalog: " + tmpFile);
    }

    if (std::rename(tmpFile.c_str(), catalogFile_.c_str()) != 0) {
        throw std::runtime_error("Could not replace catalog file: " + catalogFile_);
    }
}

void CatalogManager::loadState(Optimizer& opt) const {
    std::ifstream in(catalogFile_);
    if (!in) {
        return;
    }

    std::string magic;
    in >> magic;
    if (magic != kCatalogMagic) {
        throw std::runtime_error("Unsupported catalog format in: " + catalogFile_);
    }

    size_t tableCount = 0;
    in >> tableCount;
    if (!in) {
        throw std::runtime_error("Malformed catalog header: " + catalogFile_);
    }

    Catalog& catalog = opt.getCatalog();
    catalog.clear();

    for (size_t t = 0; t < tableCount; t++) {
        std::string tableName;
        std::string dbFilePath;
        in >> std::quoted(tableName) >> std::quoted(dbFilePath);

        size_t columnCount = 0;
        in >> columnCount;

        std::vector<ColumnDef> columns;
        columns.reserve(columnCount);
        for (size_t c = 0; c < columnCount; c++) {
            std::string columnName;
            std::string typeName;
            int size = 0;
            in >> std::quoted(columnName) >> typeName >> size;
            columns.emplace_back(columnName, dataTypeFromString(typeName), size);
        }

        int numRows = 0;
        int rootPageId = -1;
        int numDataPages = 0;
        size_t heapSize = 0;
        in >> numRows >> rootPageId >> numDataPages >> heapSize;

        auto table = std::make_unique<Table>(tableName, dbFilePath, Schema(std::move(columns)),
                                             rootPageId, 0, numDataPages);

        for (size_t i = 0; i < heapSize; i++) {
            Record record;
            int deleted = 0;
            size_t valueCount = 0;
            in >> record._record_id >> deleted >> valueCount;
            record.values.reserve(valueCount);
            for (size_t v = 0; v < valueCount; v++) {
                record.values.push_back(readValue(in));
            }
            if (deleted) {
                record.deleted_ = true;
            }
            table->loadPersistedRecord(record);
        }

        if (!in) {
            throw std::runtime_error("Malformed table entry in catalog: " + tableName);
        }

        if (table->getNumRows() != numRows) {
            // Prefer the deserialized heap truth, but make corruption visible.
            throw std::runtime_error("Catalog row-count mismatch for table: " + tableName);
        }

        // The catalog heap is the source of truth. Rebuild the B+ tree so
        // inserts/searches never depend on stale or cross-table root pages.
        table->rebuildIndex();

        catalog.addOwnedTable(std::move(table));
    }
}
