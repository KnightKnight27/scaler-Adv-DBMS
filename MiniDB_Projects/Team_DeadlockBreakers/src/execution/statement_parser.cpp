#include "execution/parser.h"
#include <stdexcept>

namespace minidb {

std::unique_ptr<CreateTableStmt> Parser::ParseCreate(const std::string& sql) {
    auto statement = std::make_unique<CreateTableStmt>();
    size_t table = ToUpper(sql).find("TABLE ");
    if (table == std::string::npos) throw std::runtime_error("CREATE missing TABLE");
    std::string rest = Trim(sql.substr(table + 6));
    size_t open = rest.find('('), close = rest.rfind(')');
    if (open == std::string::npos || close <= open)
        throw std::runtime_error("Invalid CREATE TABLE columns");
    statement->table_name = Trim(rest.substr(0, open));
    for (const auto& definition : Split(rest.substr(open + 1, close - open - 1), ',')) {
        auto tokens = Split(definition, ' ');
        if (tokens.size() < 2) throw std::runtime_error("Invalid column: " + definition);
        statement->columns.push_back({tokens[0], ParseColumnType(tokens[1])});
    }
    if (statement->columns.empty()) throw std::runtime_error("Table needs columns");
    return statement;
}

static void ParseJoinSide(const std::string& text, const std::string& default_table,
                          std::string& table, std::string& column) {
    size_t dot = text.find('.');
    table = dot == std::string::npos ? default_table : text.substr(0, dot);
    column = dot == std::string::npos ? text : text.substr(dot + 1);
}

std::unique_ptr<SelectStmt> Parser::ParseSelect(const std::string& sql) {
    auto statement = std::make_unique<SelectStmt>();
    std::string upper = ToUpper(sql);
    size_t from = upper.find(" FROM ");
    if (from == std::string::npos) throw std::runtime_error("SELECT missing FROM");
    statement->columns = Split(Trim(sql.substr(6, from - 6)), ',');
    std::string rest = Trim(sql.substr(from + 6));
    std::string upper_rest = ToUpper(rest);
    size_t join = upper_rest.find(" JOIN ");
    size_t where = upper_rest.find(" WHERE ");

    if (join == std::string::npos) {
        statement->table_name = Trim(rest.substr(0, where));
        if (where != std::string::npos)
            statement->where_clause = ParseWhereClause(rest.substr(where + 7));
        return statement;
    }

    statement->has_join = true;
    statement->table_name = Trim(rest.substr(0, join));
    std::string joined = Trim(rest.substr(join + 6));
    size_t on = ToUpper(joined).find(" ON ");
    if (on == std::string::npos) throw std::runtime_error("JOIN missing ON");
    statement->join_table = Trim(joined.substr(0, on));
    std::string condition = Trim(joined.substr(on + 4));
    size_t join_where = ToUpper(condition).find(" WHERE ");
    if (join_where != std::string::npos) {
        statement->where_clause = ParseWhereClause(condition.substr(join_where + 7));
        condition = Trim(condition.substr(0, join_where));
    }
    size_t equals = condition.find('=');
    if (equals == std::string::npos) throw std::runtime_error("JOIN condition missing =");
    ParseJoinSide(Trim(condition.substr(0, equals)), statement->table_name,
        statement->join_cond.left_table, statement->join_cond.left_column);
    ParseJoinSide(Trim(condition.substr(equals + 1)), statement->join_table,
        statement->join_cond.right_table, statement->join_cond.right_column);
    return statement;
}

std::unique_ptr<InsertStmt> Parser::ParseInsert(const std::string& sql) {
    auto statement = std::make_unique<InsertStmt>();
    std::string upper = ToUpper(sql);
    size_t into = upper.find("INTO "), values = upper.find(" VALUES");
    if (into == std::string::npos || values == std::string::npos)
        throw std::runtime_error("Invalid INSERT");
    statement->table_name = Trim(sql.substr(into + 5, values - into - 5));
    size_t open = sql.find('(', values), close = sql.rfind(')');
    if (open == std::string::npos || close <= open)
        throw std::runtime_error("INSERT values need parentheses");
    for (const auto& value : Split(sql.substr(open + 1, close - open - 1), ','))
        statement->values.push_back(ParseValue(value));
    return statement;
}

std::unique_ptr<DeleteStmt> Parser::ParseDelete(const std::string& sql) {
    auto statement = std::make_unique<DeleteStmt>();
    std::string rest = Trim(sql.substr(6));
    if (ToUpper(rest).rfind("FROM ", 0) != 0)
        throw std::runtime_error("DELETE missing FROM");
    rest = Trim(rest.substr(5));
    size_t where = ToUpper(rest).find(" WHERE ");
    statement->table_name = Trim(rest.substr(0, where));
    if (where != std::string::npos)
        statement->where_clause = ParseWhereClause(rest.substr(where + 7));
    return statement;
}

} // namespace minidb
