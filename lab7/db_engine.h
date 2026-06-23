#pragma once
#include "sql_parser.h"
#include <string>
#include <vector>
#include <unordered_map>

using Row = std::unordered_map<std::string, std::string>;

struct Table {
    std::string name;
    std::vector<std::string> columns;
    std::vector<Row> rows;

    // Add a row to the table matching schema column count
    void insertRow(const std::vector<std::string>& vals);
};

class Database {
private:
    std::unordered_map<std::string, Table> tables;

public:
    // Create a new empty table with schema columns
    void createTable(const std::string& name, const std::vector<std::string>& columns);
    
    // Insert values into a table
    void insertInto(const std::string& tableName, const std::vector<std::string>& vals);
    
    // Get table reference
    const Table* getTable(const std::string& name) const;

    // Parse, evaluate, and execute a SQL SELECT query, printing results in an aligned table
    void executeQuery(const std::string& sql) const;
};
