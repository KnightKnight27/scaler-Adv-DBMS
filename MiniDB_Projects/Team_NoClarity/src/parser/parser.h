#ifndef PARSER_H
#define PARSER_H

#include "parser/lexer.h"
#include <vector>
#include <string>

namespace minidb {

struct SQLSelectStatement {
    std::vector<std::string> projection_cols;
    std::string table_name;
    std::string filter_col;
    std::string filter_op; // "=", ">", "<"
    int filter_val{0};
};

class SQLParser {
public:
    explicit SQLParser(std::vector<Token> tokens);
    SQLSelectStatement ParseSelect();

private:
    Token Current();
    Token Consume(TokenType expected);

    std::vector<Token> tokens_;
    size_t pos_{0};
};

} // namespace minidb

#endif // PARSER_H
