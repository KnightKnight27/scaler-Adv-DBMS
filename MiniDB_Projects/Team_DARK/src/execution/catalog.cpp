#include "execution/catalog.h"

#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace minidb {

namespace {

constexpr page_id_t kCatalogMetaPageStart = 10000;

std::vector<std::string> Split(const std::string& payload, char delim) {
    std::vector<std::string> parts;
    std::stringstream stream(payload);
    std::string item;
    while (std::getline(stream, item, delim)) {
        parts.push_back(item);
    }
    return parts;
}

const ColumnDef* FindColumn(const TableDef& table, const std::string& name) {
    for (const ColumnDef& column : table.columns) {
        if (column.name == name) {
            return &column;
        }
    }
    return nullptr;
}

}  // namespace

Catalog::Catalog(BufferPoolManager* bpm)
    : bpm_(bpm), next_meta_page_id_(kCatalogMetaPageStart) {
    if (bpm_ == nullptr) {
        throw std::invalid_argument("bpm must not be null");
    }
}

TableDef Catalog::DefaultUsersTable() {
    TableDef table;
    table.name = "users";
    table.primary_key = "id";
    table.columns = {
        {"id", ColumnType::INT, true},
        {"name", ColumnType::STRING, false},
        {"age", ColumnType::INT, false},
    };
    return table;
}

void Catalog::RegisterTable(TableDef table_def) {
    const std::string table_name = table_def.name;
    tables_[table_name] = std::move(table_def);
    table_row_keys_[table_name] = {};

    const TableDef& registered = tables_.at(table_name);
    for (const ColumnDef& column : registered.columns) {
        if (column.indexed) {
            const std::string key = IndexKey(table_name, column.name);
            indexes_[key] = std::make_unique<BTree>(bpm_, NextMetaPageId(), 32);
        }
    }
}

const TableDef* Catalog::GetTable(const std::string& name) const {
    const auto it = tables_.find(name);
    if (it == tables_.end()) {
        return nullptr;
    }
    return &it->second;
}

std::string Catalog::MakeRowKey(const std::string& table, int64_t pk) const {
    return table + ":" + std::to_string(pk);
}

std::string Catalog::SerializeRow(
    const std::string& table, const std::unordered_map<std::string, Value>& values) const {
    const TableDef* table_def = GetTable(table);
    if (table_def == nullptr) {
        throw std::runtime_error("Unknown table: " + table);
    }

    std::ostringstream out;
    bool first = true;
    for (const ColumnDef& column : table_def->columns) {
        if (!first) {
            out << '\t';
        }
        first = false;
        const auto it = values.find(column.name);
        if (it == values.end()) {
            out << "";
            continue;
        }
        if (it->second.type == ValueType::INT) {
            out << it->second.int_val;
        } else {
            out << it->second.str_val;
        }
    }
    return out.str();
}

Row Catalog::DeserializeRow(const std::string& table, const std::string& row_key,
                            const std::string& payload) const {
    const TableDef* table_def = GetTable(table);
    if (table_def == nullptr) {
        throw std::runtime_error("Unknown table: " + table);
    }

    const std::vector<std::string> parts = Split(payload, '\t');
    Row row;
    row.table = table;
    row.row_key = row_key;

    for (std::size_t i = 0; i < table_def->columns.size(); ++i) {
        Value value;
        const ColumnDef& column = table_def->columns[i];
        const std::string& raw = i < parts.size() ? parts[i] : "";
        if (column.type == ColumnType::INT) {
            value.type = ValueType::INT;
            value.int_val = raw.empty() ? 0 : std::stoll(raw);
        } else {
            value.type = ValueType::STRING;
            value.str_val = raw;
        }
        row.values[column.name] = value;
    }
    return row;
}

void Catalog::TrackRow(const std::string& table, const std::string& row_key) {
    table_row_keys_[table].push_back(row_key);
}

void Catalog::UntrackRow(const std::string& table, const std::string& row_key) {
    auto& keys = table_row_keys_[table];
    keys.erase(std::remove(keys.begin(), keys.end(), row_key), keys.end());
}

std::vector<std::string> Catalog::GetRowKeys(const std::string& table) const {
    const auto it = table_row_keys_.find(table);
    if (it == table_row_keys_.end()) {
        return {};
    }
    return it->second;
}

bool Catalog::HasIndex(const std::string& table, const std::string& column) const {
    return indexes_.find(IndexKey(table, column)) != indexes_.end();
}

BTree* Catalog::GetIndex(const std::string& table, const std::string& column) {
    const auto it = indexes_.find(IndexKey(table, column));
    if (it == indexes_.end()) {
        return nullptr;
    }
    return it->second.get();
}

void Catalog::IndexInsert(const std::string& table, const std::string& column, int64_t key,
                          const RecordId& rid, const std::string& row_key) {
    BTree* index = GetIndex(table, column);
    if (index == nullptr) {
        throw std::runtime_error("Missing index for column: " + column);
    }
    index->Insert(key, rid);
    rid_to_row_key_[RidKey(rid)] = row_key;
}

void Catalog::IndexRemove(const std::string& table, const std::string& column, int64_t key) {
    BTree* index = GetIndex(table, column);
    if (index == nullptr) {
        return;
    }
    RecordId rid{};
    if (index->Search(key, &rid)) {
        rid_to_row_key_.erase(RidKey(rid));
    }
    index->Remove(key);
}

std::string Catalog::FindRowKeyByRecordId(const RecordId& rid) const {
    const auto it = rid_to_row_key_.find(RidKey(rid));
    if (it == rid_to_row_key_.end()) {
        return "";
    }
    return it->second;
}

void Catalog::RebuildFromTableHeap(const TableHeap& heap) {
    for (const std::string& row_key : heap.ListKeys()) {
        const std::size_t colon = row_key.find(':');
        if (colon == std::string::npos || colon == 0) {
            continue;
        }

        const std::string table = row_key.substr(0, colon);
        if (GetTable(table) == nullptr) {
            continue;
        }

        int64_t pk = 0;
        try {
            pk = std::stoll(row_key.substr(colon + 1));
        } catch (const std::exception&) {
            continue;
        }

        const std::vector<StoredRowVersion> versions = heap.GetVersions(row_key);
        if (versions.empty()) {
            continue;
        }

        TrackRow(table, row_key);

        const TableDef* table_def = GetTable(table);
        if (table_def != nullptr && HasIndex(table, table_def->primary_key)) {
            RecordId rid{};
            rid.page_id = versions.front().location.page_id;
            rid.slot_id = versions.front().location.slot_index;
            IndexInsert(table, table_def->primary_key, pk, rid, row_key);
        }
    }
}

std::string Catalog::RidKey(const RecordId& rid) const {
    return std::to_string(rid.page_id) + ":" + std::to_string(rid.slot_id);
}

int64_t Catalog::EstimateTableCardinality(const std::string& table) const {
    const auto it = table_row_keys_.find(table);
    if (it == table_row_keys_.end()) {
        return 0;
    }
    return static_cast<int64_t>(it->second.size());
}

double Catalog::EstimateSelectivity(const std::string& column, const std::string& op,
                                    int64_t bound, int64_t table_size) const {
    (void)column;
    if (table_size <= 0) {
        return 0.0;
    }
    if (op == "=") {
        return 1.0 / static_cast<double>(table_size);
    }
    if (op == ">") {
        return 0.33;
    }
    if (op == "<") {
        return static_cast<double>(bound) / static_cast<double>(table_size + 1);
    }
    return 0.5;
}

std::string Catalog::IndexKey(const std::string& table, const std::string& column) const {
    return table + "." + column;
}

page_id_t Catalog::NextMetaPageId() {
    return next_meta_page_id_++;
}

}  // namespace minidb
