// sql_engine.cpp  —  ADBMS Lab 6  |  Patel Jash  |  24bcs10632
//
// Core implementation of the miniature SQL SELECT processor.

#include "sql_engine.h"

#include <algorithm>
#include <cctype>
#include <ostream>
#include <stack>
#include <stdexcept>
#include <string>

namespace sql_processor {

namespace {

std::string uppercase_str(std::string str) {
    for (char& c : str)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return str;
}

bool is_identifier_start(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}
bool is_identifier_body(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

struct OperatorDetails { int priority; bool is_unary; bool is_right_assoc; };

OperatorDetails fetch_op_details(const std::string& op_sym) {
    if (op_sym == "=" || op_sym == "!=" ||
        op_sym == "<" || op_sym == "<=" ||
        op_sym == ">" || op_sym == ">=")   return {4, false, false};

    if (op_sym == "NOT")                   return {3, true,  true };
    if (op_sym == "AND")                   return {2, false, false};
    if (op_sym == "OR")                    return {1, false, false};

    return {0, false, false};
}

bool evaluate_boolean(const DataCell& cell) {
    if (std::holds_alternative<long long>(cell))
        return std::get<long long>(cell) != 0;
    return !std::get<std::string>(cell).empty();
}

int compare_cells(const DataCell& left_val, const DataCell& right_val, bool& is_valid) {
    is_valid = true;

    if (std::holds_alternative<long long>(left_val) &&
        std::holds_alternative<long long>(right_val)) {
        long long l = std::get<long long>(left_val);
        long long r = std::get<long long>(right_val);
        return (l < r) ? -1 : (l > r) ? 1 : 0;
    }

    if (std::holds_alternative<std::string>(left_val) &&
        std::holds_alternative<std::string>(right_val)) {
        const std::string& l = std::get<std::string>(left_val);
        const std::string& r = std::get<std::string>(right_val);
        return (l < r) ? -1 : (l > r) ? 1 : 0;
    }

    is_valid = false;
    return 0;
}

DataCell extract_value(const SQLToken& token,
                       const Relation& schema,
                       const Tuple& row) {
    switch (token.type) {
        case TokenType::IntLiteral:
            return DataCell{token.number};

        case TokenType::StringLiteral:
            return DataCell{token.text};

        case TokenType::Identifier: {
            int idx = schema.find_column_index(token.text);
            if (idx < 0)
                throw std::runtime_error("Column not found: " + token.text);
            return row.columns[static_cast<std::size_t>(idx)];
        }

        default:
            throw std::runtime_error("Invalid operand token: " + token.text);
    }
}

}  // anonymous namespace

int Relation::find_column_index(const std::string& col) const {
    for (std::size_t i = 0; i < col_names.size(); ++i)
        if (col_names[i] == col) return static_cast<int>(i);
    return -1;
}

std::vector<SQLToken> scan_tokens(const std::string& sql_text) {
    std::vector<SQLToken> token_stream;
    const int length = static_cast<int>(sql_text.size());
    int idx = 0;

    while (idx < length) {
        char c = sql_text[idx];

        if (std::isspace(static_cast<unsigned char>(c))) { ++idx; continue; }

        if (is_identifier_start(c)) {
            int start_idx = idx;
            while (idx < length && is_identifier_body(sql_text[idx])) ++idx;

            std::string word = sql_text.substr(static_cast<std::size_t>(start_idx),
                                               static_cast<std::size_t>(idx - start_idx));
            std::string upper_word = uppercase_str(word);

            if      (upper_word == "SELECT") token_stream.push_back({TokenType::KeySelect, upper_word});
            else if (upper_word == "FROM")   token_stream.push_back({TokenType::KeyFrom,   upper_word});
            else if (upper_word == "WHERE")  token_stream.push_back({TokenType::KeyWhere,  upper_word});
            else if (upper_word == "ORDER")  token_stream.push_back({TokenType::KeyOrder,  upper_word});
            else if (upper_word == "BY")     token_stream.push_back({TokenType::KeyBy,     upper_word});
            else if (upper_word == "ASC")    token_stream.push_back({TokenType::KeyAsc,    upper_word});
            else if (upper_word == "DESC")   token_stream.push_back({TokenType::KeyDesc,   upper_word});
            else if (upper_word == "LIMIT")  token_stream.push_back({TokenType::KeyLimit,  upper_word});
            else if (upper_word == "AND" || upper_word == "OR" || upper_word == "NOT")
                                             token_stream.push_back({TokenType::Operator, upper_word});
            else                             token_stream.push_back({TokenType::Identifier, word});
            continue;
        }

        if (std::isdigit(static_cast<unsigned char>(c))) {
            int start_idx = idx;
            while (idx < length && std::isdigit(static_cast<unsigned char>(sql_text[idx]))) ++idx;
            SQLToken t{TokenType::IntLiteral,
                       sql_text.substr(static_cast<std::size_t>(start_idx),
                                       static_cast<std::size_t>(idx - start_idx))};
            t.number = std::stoll(t.text);
            token_stream.push_back(t);
            continue;
        }

        if (c == '\'') {
            std::string literal_val;
            ++idx;
            while (idx < length) {
                if (sql_text[idx] == '\'') {
                    if (idx + 1 < length && sql_text[idx + 1] == '\'') {
                        literal_val += '\'';
                        idx += 2;
                        continue;
                    }
                    ++idx;
                    break;
                }
                literal_val += sql_text[idx++];
            }
            token_stream.push_back({TokenType::StringLiteral, literal_val});
            continue;
        }

        switch (c) {
            case '(': token_stream.push_back({TokenType::LeftParen, "("}); ++idx; break;
            case ')': token_stream.push_back({TokenType::RightParen, ")"}); ++idx; break;
            case ',': token_stream.push_back({TokenType::Comma,  ","}); ++idx; break;
            case '*': token_stream.push_back({TokenType::Asterisk,   "*"}); ++idx; break;
            case '=': token_stream.push_back({TokenType::Operator,     "="}); ++idx; break;

            case '!':
                if (idx + 1 < length && sql_text[idx + 1] == '=') {
                    token_stream.push_back({TokenType::Operator, "!="});
                    idx += 2;
                } else {
                    throw std::runtime_error("Unexpected '!' character");
                }
                break;

            case '<':
                if (idx + 1 < length && sql_text[idx + 1] == '=') {
                    token_stream.push_back({TokenType::Operator, "<="});
                    idx += 2;
                } else {
                    token_stream.push_back({TokenType::Operator, "<"});
                    ++idx;
                }
                break;

            case '>':
                if (idx + 1 < length && sql_text[idx + 1] == '=') {
                    token_stream.push_back({TokenType::Operator, ">="});
                    idx += 2;
                } else {
                    token_stream.push_back({TokenType::Operator, ">"});
                    ++idx;
                }
                break;

            default:
                throw std::runtime_error(std::string("Unexpected character: ") + c);
        }
    }

    token_stream.push_back({TokenType::EndOfFile, ""});
    return token_stream;
}

std::vector<SQLToken> convert_to_postfix(const std::vector<SQLToken>& infix_seq) {
    std::vector<SQLToken> postfix_out;
    std::stack<SQLToken> operator_stack;

    for (const SQLToken& token : infix_seq) {
        switch (token.type) {
            case TokenType::Identifier:
            case TokenType::IntLiteral:
            case TokenType::StringLiteral:
                postfix_out.push_back(token);
                break;

            case TokenType::LeftParen:
                operator_stack.push(token);
                break;

            case TokenType::RightParen:
                while (!operator_stack.empty() && operator_stack.top().type != TokenType::LeftParen) {
                    postfix_out.push_back(operator_stack.top());
                    operator_stack.pop();
                }
                if (operator_stack.empty())
                    throw std::runtime_error("Mismatched closing parenthesis");
                operator_stack.pop();
                break;

            case TokenType::Operator: {
                OperatorDetails current_op = fetch_op_details(token.text);
                while (!operator_stack.empty() && operator_stack.top().type == TokenType::Operator) {
                    OperatorDetails top_op = fetch_op_details(operator_stack.top().text);
                    bool pop_condition = (top_op.priority > current_op.priority) ||
                                         (top_op.priority == current_op.priority && !current_op.is_right_assoc);
                    if (!pop_condition) break;
                    postfix_out.push_back(operator_stack.top());
                    operator_stack.pop();
                }
                operator_stack.push(token);
                break;
            }

            case TokenType::EndOfFile:
                break;

            default:
                throw std::runtime_error("Invalid token found in expression: " + token.text);
        }
    }

    while (!operator_stack.empty()) {
        if (operator_stack.top().type == TokenType::LeftParen)
            throw std::runtime_error("Mismatched opening parenthesis");
        postfix_out.push_back(operator_stack.top());
        operator_stack.pop();
    }

    return postfix_out;
}

bool evaluate_postfix(const std::vector<SQLToken>& rpn_seq,
                      const Relation& schema, const Tuple& row) {
    std::stack<DataCell> computation_stack;

    for (const SQLToken& token : rpn_seq) {
        if (token.type != TokenType::Operator) {
            computation_stack.push(extract_value(token, schema, row));
            continue;
        }

        OperatorDetails meta = fetch_op_details(token.text);

        if (meta.is_unary) {
            if (computation_stack.empty())
                throw std::runtime_error("NOT operator lacks an operand");
            bool val = evaluate_boolean(computation_stack.top());
            computation_stack.pop();
            computation_stack.push(DataCell{static_cast<long long>(!val)});
            continue;
        }

        if (computation_stack.size() < 2)
            throw std::runtime_error("Missing operands for binary operator: " + token.text);

        DataCell rhs_val = computation_stack.top(); computation_stack.pop();
        DataCell lhs_val = computation_stack.top(); computation_stack.pop();

        if (token.text == "AND") {
            computation_stack.push(DataCell{static_cast<long long>(
                evaluate_boolean(lhs_val) && evaluate_boolean(rhs_val))});
            continue;
        }
        if (token.text == "OR") {
            computation_stack.push(DataCell{static_cast<long long>(
                evaluate_boolean(lhs_val) || evaluate_boolean(rhs_val))});
            continue;
        }

        bool type_match = true;
        int cmp_res = compare_cells(lhs_val, rhs_val, type_match);
        bool final_res = false;

        if (type_match) {
            if      (token.text == "=")  final_res = (cmp_res == 0);
            else if (token.text == "!=") final_res = (cmp_res != 0);
            else if (token.text == "<")  final_res = (cmp_res <  0);
            else if (token.text == "<=") final_res = (cmp_res <= 0);
            else if (token.text == ">")  final_res = (cmp_res >  0);
            else if (token.text == ">=") final_res = (cmp_res >= 0);
        }

        computation_stack.push(DataCell{static_cast<long long>(final_res)});
    }

    if (computation_stack.empty()) return true;
    return evaluate_boolean(computation_stack.top());
}

std::string postfix_to_string(const std::vector<SQLToken>& rpn_seq) {
    std::string result_str;
    for (const SQLToken& token : rpn_seq) {
        if (!result_str.empty()) result_str += ' ';
        result_str += (token.type == TokenType::StringLiteral) ? ("'" + token.text + "'") : token.text;
    }
    return result_str;
}

QueryStructure parse_query(const std::string& sql_text) {
    std::vector<SQLToken> stream = scan_tokens(sql_text);
    QueryStructure structure;
    std::size_t pointer = 0;

    auto require_token = [&](TokenType expected_type, const char* error_msg) {
        if (stream[pointer].type != expected_type)
            throw std::runtime_error(std::string("Syntax error, expected: ") + error_msg);
    };

    require_token(TokenType::KeySelect, "SELECT clause");
    ++pointer;

    if (stream[pointer].type == TokenType::Asterisk) {
        ++pointer;
    } else {
        while (true) {
            require_token(TokenType::Identifier, "column identifier");
            structure.target_cols.push_back(stream[pointer].text);
            ++pointer;
            if (stream[pointer].type == TokenType::Comma) { ++pointer; continue; }
            break;
        }
    }

    require_token(TokenType::KeyFrom, "FROM clause");  ++pointer;
    require_token(TokenType::Identifier,  "source table name");
    structure.source_table = stream[pointer].text;
    ++pointer;

    if (stream[pointer].type == TokenType::KeyWhere) {
        ++pointer;
        std::vector<SQLToken> infix_condition;
        while (stream[pointer].type != TokenType::KeyOrder  &&
               stream[pointer].type != TokenType::KeyLimit  &&
               stream[pointer].type != TokenType::EndOfFile)
            infix_condition.push_back(stream[pointer++]);
        structure.where_rpn = convert_to_postfix(infix_condition);
    }

    if (stream[pointer].type == TokenType::KeyOrder) {
        ++pointer;
        require_token(TokenType::KeyBy, "BY keyword");
        ++pointer;
        require_token(TokenType::Identifier, "sorting column");
        structure.order_col = stream[pointer].text;
        ++pointer;
        if      (stream[pointer].type == TokenType::KeyAsc)  ++pointer;
        else if (stream[pointer].type == TokenType::KeyDesc) { structure.descending = true; ++pointer; }
    }

    if (stream[pointer].type == TokenType::KeyLimit) {
        ++pointer;
        require_token(TokenType::IntLiteral, "limit count");
        structure.limit_rows = stream[pointer].number;
        ++pointer;
    }

    require_token(TokenType::EndOfFile, "end of statement");
    return structure;
}

Relation run_query(const QueryStructure& query, const Relation& db_table) {
    if (query.source_table != db_table.title)
        throw std::runtime_error("Table does not exist: " + query.source_table);

    std::vector<std::string> final_header =
        query.target_cols.empty() ? db_table.col_names : query.target_cols;

    std::vector<int> col_mapping;
    col_mapping.reserve(final_header.size());
    for (const std::string& col_name : final_header) {
        int idx = db_table.find_column_index(col_name);
        if (idx < 0)
            throw std::runtime_error("Requested column not in schema: " + col_name);
        col_mapping.push_back(idx);
    }

    Relation result_set;
    result_set.title   = db_table.title;
    result_set.col_names = final_header;

    for (const Tuple& row : db_table.rows) {
        if (!query.where_rpn.empty() &&
            !evaluate_postfix(query.where_rpn, db_table, row))
            continue;

        Tuple new_tuple;
        new_tuple.columns.reserve(col_mapping.size());
        for (int c_idx : col_mapping)
            new_tuple.columns.push_back(row.columns[static_cast<std::size_t>(c_idx)]);
        result_set.rows.push_back(std::move(new_tuple));
    }

    if (!query.order_col.empty()) {
        int order_idx = result_set.find_column_index(query.order_col);
        if (order_idx < 0)
            throw std::runtime_error("Sorting column not in result set: " + query.order_col);

        std::stable_sort(result_set.rows.begin(), result_set.rows.end(),
            [&](const Tuple& row_a, const Tuple& row_b) {
                bool type_valid = true;
                int cmp_val = compare_cells(row_a.columns[static_cast<std::size_t>(order_idx)],
                                            row_b.columns[static_cast<std::size_t>(order_idx)],
                                            type_valid);
                return query.descending ? (cmp_val > 0) : (cmp_val < 0);
            });
    }

    if (query.limit_rows >= 0 && static_cast<long long>(result_set.rows.size()) > query.limit_rows)
        result_set.rows.resize(static_cast<std::size_t>(query.limit_rows));

    return result_set;
}

void render_relation(const Relation& rel, std::ostream& out) {
    const std::size_t col_count = rel.col_names.size();

    std::vector<std::size_t> widths(col_count);
    for (std::size_t i = 0; i < col_count; ++i)
        widths[i] = rel.col_names[i].size();

    auto format_val = [](const DataCell& cell) -> std::string {
        if (std::holds_alternative<long long>(cell))
            return std::to_string(std::get<long long>(cell));
        return std::get<std::string>(cell);
    };

    for (const Tuple& row : rel.rows)
        for (std::size_t i = 0; i < col_count; ++i)
            widths[i] = std::max(widths[i], format_val(row.columns[i]).size());

    auto print_align = [&](const std::string& txt, std::size_t w) {
        out << txt << std::string(w - txt.size(), ' ');
    };

    for (std::size_t i = 0; i < col_count; ++i) {
        print_align(rel.col_names[i], widths[i]);
        out << (i + 1 < col_count ? " | " : "\n");
    }

    for (std::size_t i = 0; i < col_count; ++i) {
        out << std::string(widths[i], '-');
        out << (i + 1 < col_count ? "-+-" : "\n");
    }

    for (const Tuple& row : rel.rows) {
        for (std::size_t i = 0; i < col_count; ++i) {
            print_align(format_val(row.columns[i]), widths[i]);
            out << (i + 1 < col_count ? " | " : "\n");
        }
    }

    out << "(" << rel.rows.size() << " row" << (rel.rows.size() == 1 ? "" : "s") << ")\n";
}

}  // namespace sql_processor
