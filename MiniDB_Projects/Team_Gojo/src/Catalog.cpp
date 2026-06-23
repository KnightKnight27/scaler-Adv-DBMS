#include "Catalog.h"

#include <stdexcept>

// Full Table definition is needed here for method calls like
// table->getNumRows(). This is safe because Catalog.cpp is a
// translation unit — no circular dependency risk.
#include "Table.h"

// ── addTable ────────────────────────────────────────────────────────────

void Catalog::addTable(const std::string& name, Table* table) {
    tables_[name] = table;
    // Initialize statistics from the table's current state.
    TableStats stats;
    stats.numRows    = table->getNumRows();
    stats.numDistinct = std::max(1, stats.numRows);  // assume all keys distinct
    stats_[name] = stats;
}

Table* Catalog::addOwnedTable(std::unique_ptr<Table> table) {
    Table* raw = table.get();
    ownedTables_.push_back(std::move(table));
    addTable(raw->getName(), raw);
    return raw;
}

// ── getTable ────────────────────────────────────────────────────────────

Table* Catalog::getTable(const std::string& name) {
    auto it = tables_.find(name);
    if (it == tables_.end()) {
        throw std::runtime_error("Table not found in catalog: " + name);
    }
    return it->second;
}

// ── hasTable ────────────────────────────────────────────────────────────

bool Catalog::hasTable(const std::string& name) const {
    return tables_.find(name) != tables_.end();
}

// ── getStats ────────────────────────────────────────────────────────────

Catalog::TableStats Catalog::getStats(const std::string& name) {
    auto it = stats_.find(name);
    if (it == stats_.end()) {
        throw std::runtime_error("No stats for table: " + name);
    }
    // Refresh numRows from the live table.
    it->second.numRows    = tables_[name]->getNumRows();
    it->second.numDistinct = std::max(1, it->second.numRows);
    return it->second;
}

// ── refreshStats ────────────────────────────────────────────────────────

void Catalog::refreshStats(const std::string& name) {
    Table* t = getTable(name);
    stats_[name].numRows    = t->getNumRows();
    stats_[name].numDistinct = std::max(1, stats_[name].numRows);
}

void Catalog::clear() {
    tables_.clear();
    stats_.clear();
    ownedTables_.clear();
}
