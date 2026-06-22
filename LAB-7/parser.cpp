#include "parser.h"
#include <unordered_map>
#include <stack>
#include <stdexcept>
#include <iostream>

const std::unordered_map<std::string, OpInfo> OPS = {
    {"||", {1, false}}, {"OR", {1, false}},
    {"&&", {2, false}}, {"AND", {2, false}},
    {"=",  {3, false}}, {"!=", {3, false}}, {"<>", {3, false}},
    {"<",  {4, false}}, {">",  {4, false}}, {"<=", {4, false}}, {">=", {4, false}},
    {"+",  {5, false}}, {"-",  {5, false}},
    {"*",  {6, false}}, {"/",  {6, false}},
    {"^",  {7, true }},
    {"NOT", {8, true }}, {"!", {8, true }}
};

static bool isOperator(const Token& tok) {
    if (tok.type == TokenType::AND || tok.type == TokenType::OR || tok.type == TokenType::NOT) return true;
    if (tok.type == TokenType::GT || tok.type == TokenType::LT || tok.type == TokenType::GTE || 
        tok.type == TokenType::LTE || tok.type == TokenType::EQ || tok.type == TokenType::NEQ) return true;
    if (tok.type == TokenType::OPERATOR || tok.type == TokenType::ASTERISK) return true;
    return false;
}

SQLParser::SQLParser(std::vector<Token> tokens) : tokens(std::move(tokens)) {}

Token SQLParser::current() {
    if (pos >= tokens.size()) {
        return {TokenType::END, ""};
    }
    return tokens[pos];
}

Token SQLParser::consume(TokenType expected, const std::string& errMsg) {
    if (current().type != expected) {
        throw std::runtime_error(errMsg + " (expected token of type " + std::to_string(static_cast<int>(expected)) + ", got " + std::to_string(static_cast<int>(current().type)) + " with text '" + current().text + "')");
    }
    return tokens[pos++];
}

bool SQLParser::match(TokenType type) {
    if (isAtEnd()) return false;
    return current().type == type;
}

void SQLParser::advance() {
    if (!isAtEnd()) pos++;
}

bool SQLParser::isAtEnd() {
    return pos >= tokens.size() || current().type == TokenType::END;
}

SelectQuery SQLParser::parseSelect() {
    SelectQuery q;
    consume(TokenType::SELECT, "Expected SELECT keyword");

    // Parse column list
    if (current().type == TokenType::ASTERISK) {
        consume(TokenType::ASTERISK, "Expected asterisk");
        // q.columns stays empty (indicating select *)
    } else {
        while (true) {
            std::string col = consume(TokenType::IDENTIFIER, "Expected column name").text;
            q.columns.push_back(col);
            if (current().type == TokenType::COMMA) {
                consume(TokenType::COMMA, "Expected comma");
            } else {
                break;
            }
        }
    }

    consume(TokenType::FROM, "Expected FROM keyword");
    q.from = consume(TokenType::IDENTIFIER, "Expected table name").text;

    // Optional WHERE
    if (current().type == TokenType::WHERE) {
        consume(TokenType::WHERE, "Expected WHERE keyword");
        std::string raw;
        while (!isAtEnd() && current().type != TokenType::ORDER && current().type != TokenType::LIMIT) {
            if (!raw.empty()) raw += " ";
            if (current().type == TokenType::STRING) {
                raw += "'" + current().text + "'";
            } else {
                raw += current().text;
            }
            advance();
        }
        q.where_raw = raw;
    }

    // Optional ORDER BY
    if (current().type == TokenType::ORDER) {
        consume(TokenType::ORDER, "Expected ORDER");
        consume(TokenType::BY, "Expected BY");
        q.order_by = consume(TokenType::IDENTIFIER, "Expected order by column name").text;
        if (current().type == TokenType::ASC) {
            consume(TokenType::ASC, "Expected ASC");
            q.order_asc = true;
        } else if (current().type == TokenType::DESC) {
            consume(TokenType::DESC, "Expected DESC");
            q.order_asc = false;
        }
    }

    // Optional LIMIT
    if (current().type == TokenType::LIMIT) {
        consume(TokenType::LIMIT, "Expected LIMIT");
        q.limit = std::stoi(consume(TokenType::NUMBER, "Expected limit count").text);
    }

    return q;
}

std::vector<RpnToken> SQLParser::toRPN(const std::vector<Token>& infixTokens) {
    std::vector<RpnToken> output;
    std::stack<Token> ops;

    for (const auto& tok : infixTokens) {
        if (tok.type == TokenType::LPAREN) {
            ops.push(tok);
        } else if (tok.type == TokenType::RPAREN) {
            while (!ops.empty() && ops.top().type != TokenType::LPAREN) {
                output.push_back({RpnTokenType::OPERATOR, Value(ops.top().text)});
                ops.pop();
            }
            if (ops.empty()) {
                throw std::runtime_error("Mismatched parentheses");
            }
            ops.pop(); // discard '('
        } else if (isOperator(tok)) {
            std::string op1_text = tok.text;
            if (tok.type == TokenType::AND) op1_text = "AND";
            else if (tok.type == TokenType::OR) op1_text = "OR";
            else if (tok.type == TokenType::NOT) op1_text = "NOT";

            if (!OPS.count(op1_text)) {
                throw std::runtime_error("Unknown operator: " + op1_text);
            }
            const auto& o1 = OPS.at(op1_text);

            while (!ops.empty() && isOperator(ops.top())) {
                std::string op2_text = ops.top().text;
                if (ops.top().type == TokenType::AND) op2_text = "AND";
                else if (ops.top().type == TokenType::OR) op2_text = "OR";
                else if (ops.top().type == TokenType::NOT) op2_text = "NOT";

                if (OPS.count(op2_text)) {
                    const auto& o2 = OPS.at(op2_text);
                    if (o2.precedence > o1.precedence ||
                       (o2.precedence == o1.precedence && !o1.right_assoc)) {
                        output.push_back({RpnTokenType::OPERATOR, Value(ops.top().text)});
                        ops.pop();
                    } else {
                        break;
                    }
                } else {
                    break;
                }
            }
            Token pushed_tok = tok;
            pushed_tok.text = op1_text;
            ops.push(pushed_tok);
        } else if (tok.type == TokenType::NUMBER) {
            output.push_back({RpnTokenType::LITERAL, Value(std::stod(tok.text))});
        } else if (tok.type == TokenType::STRING) {
            output.push_back({RpnTokenType::LITERAL, Value(tok.text)});
        } else if (tok.type == TokenType::IDENTIFIER) {
            output.push_back({RpnTokenType::COLUMN_REF, Value(tok.text)});
        }
    }

    while (!ops.empty()) {
        if (ops.top().type == TokenType::LPAREN) {
            throw std::runtime_error("Mismatched parentheses");
        }
        output.push_back({RpnTokenType::OPERATOR, Value(ops.top().text)});
        ops.pop();
    }

    return output;
}
