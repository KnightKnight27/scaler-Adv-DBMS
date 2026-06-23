#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace minidb {

enum class StatementType { SELECT, INSERT, DELETE, INVALID };

struct SQLStatement {
    StatementType type_{StatementType::INVALID};
    std::string table_name_;
    bool is_replica_{false};
    
    // For SELECT WHERE id = X
    bool has_where_{false};
    int32_t where_id_{-1};
    
    // For INSERT
    int32_t insert_id_{-1};
    std::string insert_name_;
};

class Parser {
public:
    // Extremely simplified regex/string split parser for academic demonstration
    static SQLStatement Parse(const std::string& query);
};

} // namespace minidb
