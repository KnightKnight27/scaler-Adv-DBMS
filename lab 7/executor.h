#pragma once

#include "types.h"
#include "select_stat.h"
#include <vector>
#include <algorithm>
#include <iostream>
#include <iomanip>

class Executor {
public:
    static std::vector<Row> execute(const SelectStatement& stmt, const std::vector<Row>& table) {
        std::vector<Row> filtered;

        // 1. Filter rows using WHERE clause
        for (const auto& row : table) {
            if (stmt.whereFilter) {
                try {
                    if (getBoolValue(stmt.whereFilter->evaluate(row))) {
                        filtered.push_back(row);
                    }
                } catch (const std::exception& e) {
                    // Ignore rows with missing fields in expression evaluation
                }
            } else {
                filtered.push_back(row);
            }
        }

        // 2. Sort rows using ORDER BY clause
        if (!stmt.orderByColumn.empty()) {
            std::sort(filtered.begin(), filtered.end(), [&](const Row& a, const Row& b) {
                auto itA = a.find(stmt.orderByColumn);
                auto itB = b.find(stmt.orderByColumn);
                if (itA == a.end()) return false;
                if (itB == b.end()) return true;

                double numA = 0.0, numB = 0.0;
                bool isNumA = false, isNumB = false;

                if (std::holds_alternative<int>(itA->second)) { numA = std::get<int>(itA->second); isNumA = true; }
                else if (std::holds_alternative<double>(itA->second)) { numA = std::get<double>(itA->second); isNumA = true; }

                if (std::holds_alternative<int>(itB->second)) { numB = std::get<int>(itB->second); isNumB = true; }
                else if (std::holds_alternative<double>(itB->second)) { numB = std::get<double>(itB->second); isNumB = true; }

                if (isNumA && isNumB) {
                    return stmt.orderByAsc ? (numA < numB) : (numA > numB);
                } else {
                    std::string strA = std::holds_alternative<std::string>(itA->second) ? std::get<std::string>(itA->second) : std::to_string(numA);
                    std::string strB = std::holds_alternative<std::string>(itB->second) ? std::get<std::string>(itB->second) : std::to_string(numB);
                    return stmt.orderByAsc ? (strA < strB) : (strA > strB);
                }
            });
        }

        // 3. Apply LIMIT
        if (stmt.limit >= 0 && static_cast<size_t>(stmt.limit) < filtered.size()) {
            filtered.resize(stmt.limit);
        }

        // 4. Project selected columns
        std::vector<Row> projected;
        for (const auto& row : filtered) {
            Row projRow;
            if (stmt.columns.empty()) {
                // SELECT *
                projRow = row;
            } else {
                for (const auto& col : stmt.columns) {
                    auto it = row.find(col);
                    if (it != row.end()) {
                        projRow[col] = it->second;
                    }
                }
            }
            projected.push_back(projRow);
        }

        return projected;
    }

    static void printTable(const std::vector<Row>& results) {
        if (results.empty()) {
            std::cout << "(No rows returned)\n\n";
            return;
        }

        // Get unique headers, prioritizing id, name, age if they exist
        std::vector<std::string> headers;
        auto addHeader = [&](const std::string& h) {
            for (const auto& row : results) {
                if (row.find(h) != row.end()) {
                    headers.push_back(h);
                    break;
                }
            }
        };

        addHeader("id");
        addHeader("name");
        addHeader("age");
        addHeader("gpa");
        addHeader("status");

        for (const auto& row : results) {
            for (const auto& [col, val] : row) {
                if (std::find(headers.begin(), headers.end(), col) == headers.end()) {
                    headers.push_back(col);
                }
            }
        }

        // Print header
        for (const auto& header : headers) {
            std::cout << std::left << std::setw(15) << header << " | ";
        }
        std::cout << "\n";
        for (size_t i = 0; i < headers.size() * 18; ++i) std::cout << "-";
        std::cout << "\n";

        // Print rows
        for (const auto& row : results) {
            for (const auto& header : headers) {
                auto it = row.find(header);
                if (it != row.end()) {
                    std::cout << std::left << std::setw(15);
                    printValue(it->second);
                    std::cout << " | ";
                } else {
                    std::cout << std::left << std::setw(15) << "NULL" << " | ";
                }
            }
            std::cout << "\n";
        }
        std::cout << "\n";
    }
};
