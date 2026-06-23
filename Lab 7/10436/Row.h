#pragma once
#include <unordered_map>
#include <string>
#include <vector>
#include <iostream>

using Row = std::unordered_map<std::string, std::string>;

inline void print_row(const Row& row) {
    for (const auto& [col, val] : row)
        std::cout << col << ": " << val << "  ";
    std::cout << "\n";
}

inline void print_rows(const std::vector<Row>& rows, const std::string& header = "") {
    if (!header.empty()) std::cout << "\n--- " << header << " ---\n";
    if (rows.empty()) { std::cout << "(no results)\n"; return; }
    for (const auto& r : rows) print_row(r);
    std::cout << rows.size() << " row(s)\n";
}
