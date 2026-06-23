#include "parser/Parser.hpp"

#include <algorithm>
#include <cctype>

using namespace std;

namespace minidb {

static string upper(const string& s) {
    string r = s;
    transform(r.begin(), r.end(), r.begin(), [](unsigned char c) { return (char)toupper(c); });
    return r;
}

Parser::Parser(const string& sql) : lexer_(sql) { advance(); }
void Parser::advance() { current_ = lexer_.nextToken(); }

bool Parser::matchKeyword(const string& kw) {
    return current_.type == TokenType::KEYWORD && upper(current_.text) == upper(kw);
}

bool Parser::expectKeyword(const string& kw) {
    if (!matchKeyword(kw)) return false;
    advance();
    return true;
}

Token Parser::expect(TokenType type) {
    Token t = current_;
    if (current_.type == type) advance();
    return t;
}

ColumnType Parser::parseColumnType(const string& text) {
    if (upper(text) == "STRING" || upper(text) == "VARCHAR") return ColumnType::STRING;
    return ColumnType::INT;
}

CompareOp Parser::tokenToOp(TokenType t) {
    switch (t) {
        case TokenType::EQ: return CompareOp::EQ;
        case TokenType::NE: return CompareOp::NE;
        case TokenType::LT: return CompareOp::LT;
        case TokenType::LE: return CompareOp::LE;
        case TokenType::GT: return CompareOp::GT;
        case TokenType::GE: return CompareOp::GE;
        default: return CompareOp::EQ;
    }
}

ParsedStatement Parser::parse() {
    ParsedStatement stmt;

    if (matchKeyword("CREATE")) {
        expectKeyword("CREATE"); expectKeyword("TABLE");
        stmt.type = StmtType::CREATE_TABLE;
        stmt.table_name = current_.text; advance();
        expect(TokenType::LPAREN);
        while (current_.type != TokenType::RPAREN && current_.type != TokenType::END) {
            ColumnSpec col;
            col.name = current_.text; advance();
            col.type = parseColumnType(current_.text); advance();
            if (matchKeyword("PRIMARY")) { expectKeyword("PRIMARY"); expectKeyword("KEY"); col.primary_key = true; }
            stmt.columns.push_back(col);
            if (current_.type == TokenType::COMMA) advance();
        }
        expect(TokenType::RPAREN);
        return stmt;
    }

    if (matchKeyword("INSERT")) {
        expectKeyword("INSERT"); expectKeyword("INTO");
        stmt.type = StmtType::INSERT;
        stmt.table_name = current_.text; advance();
        expectKeyword("VALUES"); expect(TokenType::LPAREN);
        vector<string> values;
        while (current_.type != TokenType::RPAREN && current_.type != TokenType::END) {
            if (current_.type == TokenType::NUMBER || current_.type == TokenType::STRING ||
                current_.type == TokenType::KEYWORD || current_.type == TokenType::IDENTIFIER) {
                values.push_back(current_.text); advance();
            }
            if (current_.type == TokenType::COMMA) advance();
        }
        expect(TokenType::RPAREN);
        for (size_t i = 0; i < values.size(); ++i)
            stmt.insert_row["col" + to_string(i + 1)] = values[i];
        return stmt;
    }

    if (matchKeyword("SELECT")) {
        expectKeyword("SELECT");
        if (current_.type == TokenType::STAR) advance();
        expectKeyword("FROM");
        stmt.type = StmtType::SELECT;
        stmt.table_name = current_.text; advance();
        if (matchKeyword("JOIN")) {
            string left = stmt.table_name;
            expectKeyword("JOIN");
            string right = current_.text; advance();
            expectKeyword("ON");
            string lc = current_.text; advance();
            expect(TokenType::EQ);
            string rc = current_.text; advance();
            stmt.has_join = true;
            stmt.join = JoinClause{left, right, lc, rc};
        }
        if (matchKeyword("WHERE")) {
            expectKeyword("WHERE");
            stmt.has_where = true;
            stmt.where.column = current_.text; advance();
            stmt.where.op = tokenToOp(current_.type); advance();
            stmt.where.value = current_.text; advance();
        }
        return stmt;
    }

    if (matchKeyword("DELETE")) {
        expectKeyword("DELETE"); expectKeyword("FROM");
        stmt.type = StmtType::DELETE;
        stmt.table_name = current_.text; advance();
        if (matchKeyword("WHERE")) {
            expectKeyword("WHERE");
            stmt.has_where = true;
            stmt.where.column = current_.text; advance();
            stmt.where.op = tokenToOp(current_.type); advance();
            stmt.where.value = current_.text; advance();
        }
        return stmt;
    }

    if (matchKeyword("BEGIN")) { stmt.type = StmtType::BEGIN; advance(); return stmt; }
    if (matchKeyword("COMMIT")) { stmt.type = StmtType::COMMIT; advance(); return stmt; }
    if (matchKeyword("ROLLBACK")) { stmt.type = StmtType::ROLLBACK; advance(); return stmt; }
    if (matchKeyword("CRASH")) { stmt.type = StmtType::CRASH; advance(); return stmt; }
    if (matchKeyword("RECOVER")) { stmt.type = StmtType::RECOVER; advance(); return stmt; }

    return stmt;
}

}  // namespace minidb
