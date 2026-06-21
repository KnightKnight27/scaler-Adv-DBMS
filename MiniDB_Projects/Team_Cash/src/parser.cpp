#include "parser.h"

#include <cctype>
#include <stdexcept>
#include <unordered_set>

namespace minidb {

static const std::unordered_set<std::string> KEYWORDS = {
    "CREATE", "TABLE", "INSERT", "INTO", "VALUES", "SELECT", "FROM",
    "WHERE", "DELETE", "JOIN", "ON", "AND", "INT", "TEXT"};

static std::string upper(const std::string& s) {
    std::string r = s;
    for (char& c : r) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return r;
}

std::vector<Token> tokenize(const std::string& sql) {
    std::vector<Token> toks;
    size_t i = 0, n = sql.size();
    while (i < n) {
        char c = sql[i];
        if (std::isspace(static_cast<unsigned char>(c))) {
            ++i;
        } else if (c == '\'') {  // string literal
            size_t j = i + 1;
            while (j < n && sql[j] != '\'') ++j;
            toks.push_back({Tok::Str, sql.substr(i + 1, j - i - 1)});
            i = j + 1;
        } else if (std::isdigit(static_cast<unsigned char>(c)) ||
                   (c == '-' && i + 1 < n && std::isdigit(static_cast<unsigned char>(sql[i + 1])))) {
            size_t j = i + 1;
            while (j < n && std::isdigit(static_cast<unsigned char>(sql[j]))) ++j;
            toks.push_back({Tok::Int, sql.substr(i, j - i)});
            i = j;
        } else if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            size_t j = i + 1;
            while (j < n && (std::isalnum(static_cast<unsigned char>(sql[j])) || sql[j] == '_')) ++j;
            std::string word = sql.substr(i, j - i);
            std::string up = upper(word);
            toks.push_back({KEYWORDS.count(up) ? Tok::Keyword : Tok::Name, KEYWORDS.count(up) ? up : word});
            i = j;
        } else if (i + 1 < n && (sql.substr(i, 2) == "<=" || sql.substr(i, 2) == ">=" || sql.substr(i, 2) == "!=")) {
            toks.push_back({Tok::Op, sql.substr(i, 2)});
            i += 2;
        } else if (c == '=' || c == '<' || c == '>') {
            toks.push_back({Tok::Op, std::string(1, c)});
            ++i;
        } else if (std::string("(),.*;").find(c) != std::string::npos) {
            toks.push_back({Tok::Sym, std::string(1, c)});
            ++i;
        } else {
            throw std::runtime_error(std::string("unexpected character: ") + c);
        }
    }
    toks.push_back({Tok::End, ""});
    return toks;
}

namespace {
class Parser {
public:
    explicit Parser(std::vector<Token> toks) : toks_(std::move(toks)) {}

    std::unique_ptr<Statement> parse() {
        const Token& h = peek();
        if (h.kind == Tok::Keyword && h.text == "CREATE") return parseCreate();
        if (h.kind == Tok::Keyword && h.text == "INSERT") return parseInsert();
        if (h.kind == Tok::Keyword && h.text == "SELECT") return parseSelect();
        if (h.kind == Tok::Keyword && h.text == "DELETE") return parseDelete();
        throw std::runtime_error("unsupported statement");
    }

private:
    std::vector<Token> toks_;
    size_t pos_ = 0;

    const Token& peek() const { return toks_[pos_]; }
    const Token& next() { return toks_[pos_++]; }

    void expectKw(const std::string& w) {
        const Token& t = next();
        if (t.kind != Tok::Keyword || t.text != w) throw std::runtime_error("expected " + w);
    }
    void expectSym(const std::string& s) {
        const Token& t = next();
        if (t.kind != Tok::Sym || t.text != s) throw std::runtime_error("expected '" + s + "'");
    }
    std::string name() {
        const Token& t = next();
        if (t.kind != Tok::Name) throw std::runtime_error("expected a name");
        return t.text;
    }

    ColRef colRef() {
        ColRef c;
        std::string first = name();
        if (peek().kind == Tok::Sym && peek().text == ".") {
            next();
            c.table = first;
            c.name = name();
        } else {
            c.name = first;
        }
        return c;
    }

    Value literal() {
        const Token& t = next();
        if (t.kind == Tok::Int) return Value::Int(std::stoll(t.text));
        if (t.kind == Tok::Str) return Value::Text(t.text);
        throw std::runtime_error("expected a value");
    }

    std::vector<Condition> parseWhere() {
        std::vector<Condition> conds;
        if (peek().kind == Tok::Keyword && peek().text == "WHERE") {
            next();
            while (true) {
                Condition c;
                c.left = colRef();
                const Token& op = next();
                if (op.kind != Tok::Op) throw std::runtime_error("expected a comparison operator");
                c.op = op.text;
                c.literal = literal();
                c.hasLiteral = true;
                conds.push_back(c);
                if (peek().kind == Tok::Keyword && peek().text == "AND") next();
                else break;
            }
        }
        return conds;
    }

    std::unique_ptr<Statement> parseCreate() {
        expectKw("CREATE");
        expectKw("TABLE");
        auto stmt = std::make_unique<CreateStmt>();
        stmt->table = name();
        expectSym("(");
        while (true) {
            std::string cname = name();
            const Token& ty = next();
            if (ty.kind != Tok::Keyword || (ty.text != "INT" && ty.text != "TEXT"))
                throw std::runtime_error("expected INT or TEXT");
            stmt->columns.push_back(Column{cname, ty.text == "INT" ? Type::INT : Type::TEXT});
            const Token& sep = next();
            if (sep.text == ")") break;
            if (sep.text != ",") throw std::runtime_error("expected ',' or ')'");
        }
        return stmt;
    }

    std::unique_ptr<Statement> parseInsert() {
        expectKw("INSERT");
        expectKw("INTO");
        auto stmt = std::make_unique<InsertStmt>();
        stmt->table = name();
        expectKw("VALUES");
        expectSym("(");
        while (true) {
            stmt->values.push_back(literal());
            const Token& sep = next();
            if (sep.text == ")") break;
            if (sep.text != ",") throw std::runtime_error("expected ',' or ')'");
        }
        return stmt;
    }

    std::unique_ptr<Statement> parseSelect() {
        expectKw("SELECT");
        auto stmt = std::make_unique<SelectStmt>();
        if (peek().kind == Tok::Sym && peek().text == "*") {
            next();
        } else {
            while (true) {
                stmt->columns.push_back(colRef());
                if (peek().kind == Tok::Sym && peek().text == ",") next();
                else break;
            }
        }
        expectKw("FROM");
        stmt->table = name();
        if (peek().kind == Tok::Keyword && peek().text == "JOIN") {
            next();
            stmt->hasJoin = true;
            stmt->joinTable = name();
            expectKw("ON");
            stmt->joinCond.left = colRef();
            const Token& op = next();
            if (op.text != "=") throw std::runtime_error("only equi-joins (=) are supported");
            stmt->joinCond.op = "=";
            stmt->joinCond.right = colRef();
            stmt->joinCond.hasRight = true;
        }
        stmt->where = parseWhere();
        return stmt;
    }

    std::unique_ptr<Statement> parseDelete() {
        expectKw("DELETE");
        expectKw("FROM");
        auto stmt = std::make_unique<DeleteStmt>();
        stmt->table = name();
        stmt->where = parseWhere();
        return stmt;
    }
};
}  // namespace

std::unique_ptr<Statement> parse(const std::string& sql) {
    return Parser(tokenize(sql)).parse();
}

}  // namespace minidb
