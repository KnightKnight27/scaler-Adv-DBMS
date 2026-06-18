#ifndef SQL_PARSER_H
#define SQL_PARSER_H

#include <string>
#include <vector>
#include <unordered_map>
#include <variant>

using Value = std::variant<double, std::string>;

struct Row {
    std::unordered_map<std::string, Value> cols;
};

struct SelectQuery {
    std::vector<std::string> columns;   
    std::string              from;      
    std::string              where_raw; 
    std::string              order_by;  
    bool                     order_asc = true;
    int                      limit = -1;
};

// Interface engine dependencies
double row_val(const Row& row, const std::string& col);
SelectQuery parse_select(const std::string& sql);
std::vector<Row> execute(const SelectQuery& q, const std::vector<Row>& data);
void print_rows(const std::vector<Row>& rows);

#endif