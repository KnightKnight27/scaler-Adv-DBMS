#include "execution/parser.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>

namespace minidb {

std::string Parser::Trim(const std::string& text) {
    size_t start = text.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = text.find_last_not_of(" \t\r\n");
    return text.substr(start, end - start + 1);
}

std::string Parser::ToUpper(const std::string& text) {
    std::string result = text;
    std::transform(result.begin(), result.end(), result.begin(),
        [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return result;
}

std::vector<std::string> Parser::Split(const std::string& text, char delimiter) {
    std::vector<std::string> parts;
    std::istringstream input(text);
    for (std::string item; std::getline(input, item, delimiter);) {
        item = Trim(item);
        if (!item.empty()) parts.push_back(item);
    }
    return parts;
}

Value Parser::ParseValue(const std::string& text) {
    std::string value = Trim(text);
    if (value.size() >= 2 &&
        ((value.front() == '\'' && value.back() == '\'') ||
         (value.front() == '"' && value.back() == '"'))) {
        return value.substr(1, value.size() - 2);
    }
    try {
        size_t used;
        int64_t integer = std::stoll(value, &used);
        if (used == value.size()) return integer;
    } catch (...) {}
    try {
        size_t used;
        double decimal = std::stod(value, &used);
        if (used == value.size()) return decimal;
    } catch (...) {}
    return value;
}

Predicate Parser::ParsePredicate(const std::string& text) {
    static const std::vector<std::pair<std::string, CmpOp>> operators{
        {"<=", CmpOp::LTE}, {">=", CmpOp::GTE}, {"!=", CmpOp::NEQ},
        {"<>", CmpOp::NEQ}, {"<", CmpOp::LT}, {">", CmpOp::GT},
        {"=", CmpOp::EQ}};
    for (const auto& item : operators) {
        size_t position = text.find(item.first);
        if (position != std::string::npos) {
            return {Trim(text.substr(0, position)), item.second,
                    ParseValue(text.substr(position + item.first.size()))};
        }
    }
    throw std::runtime_error("Cannot parse predicate: " + text);
}

ColumnType Parser::ParseColumnType(const std::string& text) {
    std::string type = ToUpper(Trim(text));
    size_t parenthesis = type.find('(');
    if (parenthesis != std::string::npos) type = type.substr(0, parenthesis);
    if (type == "INT" || type == "INTEGER" || type == "BIGINT") return ColumnType::INT;
    if (type == "DOUBLE" || type == "FLOAT" || type == "REAL" || type == "DECIMAL")
        return ColumnType::DOUBLE;
    if (type == "STRING" || type == "TEXT" || type == "VARCHAR" || type == "CHAR")
        return ColumnType::STRING;
    throw std::runtime_error("Unknown column type: " + text);
}

WhereClause Parser::ParseWhereClause(const std::string& text) {
    WhereClause clause;
    std::string upper = ToUpper(text);
    std::string separator = upper.find(" OR ") != std::string::npos ? " OR " : " AND ";
    clause.logic = separator == " OR " ? LogicOp::OR : LogicOp::AND;
    size_t start = 0;
    for (;;) {
        size_t next = upper.find(separator, start);
        std::string part = Trim(text.substr(start,
            next == std::string::npos ? next : next - start));
        if (!part.empty()) clause.preds.push_back(ParsePredicate(part));
        if (next == std::string::npos) break;
        start = next + separator.size();
    }
    return clause;
}

std::unique_ptr<Statement> Parser::Parse(const std::string& sql) {
    std::string text = Trim(sql);
    if (!text.empty() && text.back() == ';') text = Trim(text.substr(0, text.size() - 1));
    std::string upper = ToUpper(text);
    if (upper.rfind("CREATE", 0) == 0) return ParseCreate(text);
    if (upper.rfind("SELECT", 0) == 0) return ParseSelect(text);
    if (upper.rfind("INSERT", 0) == 0) return ParseInsert(text);
    if (upper.rfind("DELETE", 0) == 0) return ParseDelete(text);
    throw std::runtime_error("Unsupported SQL: " + sql);
}

} // namespace minidb
