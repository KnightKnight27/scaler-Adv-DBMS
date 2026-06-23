#include "db_engine.h"
#include <iostream>
#include <iomanip>
#include <stdexcept>
#include <algorithm>

void Table::insertRow(const std::vector<std::string>& vals) {
    if (vals.size() != columns.size()) {
        throw std::runtime_error("Column count mismatch on insert. Expected " + 
                                 std::to_string(columns.size()) + " columns, got " + 
                                 std::to_string(vals.size()) + ".");
    }
    Row r;
    for (size_t i = 0; i < columns.size(); ++i) {
        r[columns[i]] = vals[i];
    }
    rows.push_back(r);
}

void Database::createTable(const std::string& name, const std::vector<std::string>& columns) {
    if (tables.find(name) != tables.end()) {
        throw std::runtime_error("Table already exists: " + name);
    }
    Table t;
    t.name = name;
    t.columns = columns;
    tables[name] = t;
}

void Database::insertInto(const std::string& tableName, const std::vector<std::string>& vals) {
    auto it = tables.find(tableName);
    if (it == tables.end()) {
        throw std::runtime_error("Table not found: " + tableName);
    }
    it->second.insertRow(vals);
}

const Table* Database::getTable(const std::string& name) const {
    auto it = tables.find(name);
    if (it != tables.end()) {
        return &(it->second);
    }
    return nullptr;
}

void Database::executeQuery(const std::string& sql) const {
    std::cout << "\nSQL> " << sql << std::endl;

    // 1. Parse Query
    ParsedQuery pq = parseSQL(sql);
    if (!pq.isValid) {
        std::cout << "Error: " << pq.errorMessage << std::endl;
        return;
    }

    // 2. Fetch Table
    const Table* table = getTable(pq.tableName);
    if (!table) {
        std::cout << "Error: Table '" << pq.tableName << "' not found in database." << std::endl;
        return;
    }

    // 3. Resolve Columns to Project
    std::vector<std::string> projCols;
    if (pq.isSelectAll) {
        projCols = table->columns;
    } else {
        // Validate columns
        for (const auto& col : pq.selectColumns) {
            auto it = std::find(table->columns.begin(), table->columns.end(), col);
            if (it == table->columns.end()) {
                std::cout << "Error: Column '" << col << "' not found in table '" << table->name << "'." << std::endl;
                return;
            }
            projCols.push_back(col);
        }
    }

    // 4. Filter Rows
    std::vector<Row> matchedRows;
    try {
        for (const auto& row : table->rows) {
            bool matches = true;
            if (pq.hasWhere) {
                matches = evaluateRPN(pq.whereRPN, row);
            }
            if (matches) {
                matchedRows.push_back(row);
            }
        }
    } catch (const std::exception& e) {
        std::cout << "Error during evaluation: " << e.what() << std::endl;
        return;
    }

    // 5. Pretty Print Table Output
    if (projCols.empty()) {
        std::cout << "Empty projection." << std::endl;
        return;
    }

    // Calculate maximum width for each projected column
    std::unordered_map<std::string, size_t> colWidths;
    for (const auto& col : projCols) {
        colWidths[col] = col.length();
    }
    for (const auto& row : matchedRows) {
        for (const auto& col : projCols) {
            auto it = row.find(col);
            std::string val = (it != row.end()) ? it->second : "";
            colWidths[col] = std::max(colWidths[col], val.length());
        }
    }

    // Construct separating horizontal border
    std::string separator = "+";
    for (const auto& col : projCols) {
        separator += std::string(colWidths[col] + 2, '-') + "+";
    }

    // Print Header
    std::cout << separator << std::endl;
    std::cout << "|";
    for (const auto& col : projCols) {
        std::cout << " " << std::left << std::setw(colWidths[col]) << col << " |";
    }
    std::cout << std::endl << separator << std::endl;

    // Print Data Rows
    for (const auto& row : matchedRows) {
        std::cout << "|";
        for (const auto& col : projCols) {
            auto it = row.find(col);
            std::string val = (it != row.end()) ? it->second : "";
            std::cout << " " << std::left << std::setw(colWidths[col]) << val << " |";
        }
        std::cout << std::endl;
    }

    std::cout << separator << std::endl;
    std::cout << matchedRows.size() << " row(s) in set." << std::endl;
}
