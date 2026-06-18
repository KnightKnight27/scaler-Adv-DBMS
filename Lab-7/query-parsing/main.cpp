#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <stdexcept>

using namespace std;

// ───────────────────────────────────────────────────────────────────
//  In-memory table: a small staff roster for the query to run on
// ───────────────────────────────────────────────────────────────────
struct StaffRow {
    string fullName;
    int recordId;
    int years;

    // resolve a column name to its int value on this row
    int intField(const string& field) const {
        if (field == "id")  return recordId;
        if (field == "age") return years;
        throw runtime_error("Unknown column for int: " + field);
    }

    string textField(const string& field) const {
        if (field == "name") return fullName;
        throw runtime_error("Unknown column for string: " + field);
    }
};

// ───────────────────────────────────────────────────────────────────
//  Lexer — chops the raw SQL text into a stream of tokens
// ───────────────────────────────────────────────────────────────────
enum class LexKind {
    KW_SELECT,
    KW_FROM,
    KW_WHERE,
    KW_OR,
    KW_AND,
    NAME,
    NUMERAL,
    GREATER,
    LESSER,
    EQUAL,
    GREATER_EQ,
    LESSER_EQ,
    PAREN_OPEN,
    PAREN_CLOSE,
    EOL
};

struct Lexeme {
    LexKind kind;
    string text;
};

class Scanner {
private:
    string source;
public:
    Scanner(string& sqlText) {
        source = sqlText;
    }

    vector<Lexeme> scan() {
        vector<Lexeme> out;
        int cursor = 0;
        while (cursor < (int)source.size()) {
            if (isspace(source[cursor])) {
                ++cursor;
                continue;
            }

            // words: keywords or identifiers
            if (isalpha(source[cursor])) {
                string word;
                while (cursor < (int)source.size() &&
                       (isalnum(source[cursor]) ||
                        source[cursor] == '_')) {
                    word += source[cursor++];
                }
                string caps = word;
                transform(caps.begin(), caps.end(), caps.begin(), ::toupper);
                if (caps == "SELECT")
                    out.push_back({LexKind::KW_SELECT, word});
                else if (caps == "FROM")
                    out.push_back({LexKind::KW_FROM, word});
                else if (caps == "WHERE")
                    out.push_back({LexKind::KW_WHERE, word});
                else if (caps == "OR")
                    out.push_back({LexKind::KW_OR, word});
                else if (caps == "AND")
                    out.push_back({LexKind::KW_AND, word});
                else
                    out.push_back({LexKind::NAME, word});

            } else if (isdigit(source[cursor])) {
                string digits;
                while (cursor < (int)source.size() &&
                       isdigit(source[cursor])) {
                    digits += source[cursor++];
                }
                out.push_back({LexKind::NUMERAL, digits});

            } else if (source[cursor] == '>') {
                if (cursor + 1 < (int)source.size() && source[cursor + 1] == '=') {
                    out.push_back({LexKind::GREATER_EQ, ">="});
                    cursor += 2;
                } else {
                    out.push_back({LexKind::GREATER, ">"});
                    ++cursor;
                }
            } else if (source[cursor] == '<') {
                if (cursor + 1 < (int)source.size() && source[cursor + 1] == '=') {
                    out.push_back({LexKind::LESSER_EQ, "<="});
                    cursor += 2;
                } else {
                    out.push_back({LexKind::LESSER, "<"});
                    ++cursor;
                }
            } else if (source[cursor] == '=') {
                out.push_back({LexKind::EQUAL, "="});
                ++cursor;
            } else if (source[cursor] == '(') {
                out.push_back({LexKind::PAREN_OPEN, "("});
                ++cursor;
            } else if (source[cursor] == ')') {
                out.push_back({LexKind::PAREN_CLOSE, ")"});
                ++cursor;
            } else {
                ++cursor; // skip unknown chars
            }
        }
        out.push_back({LexKind::EOL, ""});
        return out;
    }
};

// ───────────────────────────────────────────────────────────────────
//  AST nodes — the parser builds a tree of these
// ───────────────────────────────────────────────────────────────────
struct Node {
    virtual ~Node() = default;
};

struct NumberNode : Node {
    int amount;
    NumberNode(int v) { amount = v; }
};

struct FieldNode : Node {
    string label;
    FieldNode(string l) : label(l) {}
};

struct BinaryNode : Node {
    string oper;
    Node* lhs;
    Node* rhs;
    BinaryNode(string oper, Node* lhs, Node* rhs)
        : oper(oper), lhs(lhs), rhs(rhs) {}
};

struct Query {
    string projection;
    string source;
    Node* condition;
};

// ───────────────────────────────────────────────────────────────────
//  Recursive-descent parser
//  Grammar:
//    query   → SELECT name FROM name WHERE or_expr
//    or_expr → and_expr (OR and_expr)*
//    and_expr→ atom (AND atom)*
//    atom    → '(' or_expr ')' | comparison
//    comparison → name (> | < | >= | <= | =) number
// ───────────────────────────────────────────────────────────────────
class GrammarParser {
public:
    GrammarParser(vector<Lexeme> lx) : lexemes(lx) {}

    Query parseQuery() {
        expect(LexKind::KW_SELECT);
        string projection = expect(LexKind::NAME).text;
        expect(LexKind::KW_FROM);
        string table = expect(LexKind::NAME).text;
        expect(LexKind::KW_WHERE);
        Node* where = parseOr();

        Query q;
        q.projection = projection;
        q.source = table;
        q.condition = where;
        return q;
    }

private:
    Node* parseOr() {
        Node* lhs = parseAnd();
        while (lexemes[idx].kind == LexKind::KW_OR) {
            expect(LexKind::KW_OR);
            Node* rhs = parseAnd();
            lhs = new BinaryNode("OR", lhs, rhs);
        }
        return lhs;
    }

    Node* parseAnd() {
        Node* lhs = parseAtom();
        while (lexemes[idx].kind == LexKind::KW_AND) {
            expect(LexKind::KW_AND);
            Node* rhs = parseAtom();
            lhs = new BinaryNode("AND", lhs, rhs);
        }
        return lhs;
    }

    Node* parseAtom() {
        if (lexemes[idx].kind == LexKind::PAREN_OPEN) {
            expect(LexKind::PAREN_OPEN);
            Node* inner = parseOr();
            expect(LexKind::PAREN_CLOSE);
            return inner;
        }
        return parseComparison();
    }

    Node* parseComparison() {
        string field = expect(LexKind::NAME).text;
        Node* leftNode = new FieldNode(field);

        string oper;
        if (lexemes[idx].kind == LexKind::GREATER_EQ) {
            oper = ">="; expect(LexKind::GREATER_EQ);
        } else if (lexemes[idx].kind == LexKind::LESSER_EQ) {
            oper = "<="; expect(LexKind::LESSER_EQ);
        } else if (lexemes[idx].kind == LexKind::GREATER) {
            oper = ">"; expect(LexKind::GREATER);
        } else if (lexemes[idx].kind == LexKind::LESSER) {
            oper = "<"; expect(LexKind::LESSER);
        } else if (lexemes[idx].kind == LexKind::EQUAL) {
            oper = "="; expect(LexKind::EQUAL);
        } else {
            throw runtime_error("Expected comparison operator (>, <, >=, <=, =)");
        }

        int amount = stoi(expect(LexKind::NUMERAL).text);
        Node* rightNode = new NumberNode(amount);
        return new BinaryNode(oper, leftNode, rightNode);
    }

    Lexeme expect(LexKind wanted) {
        if (lexemes[idx].kind != wanted)
            throw runtime_error("invalid token format");
        return lexemes[idx++];
    }

    vector<Lexeme> lexemes;
    int idx = 0;
};

// ───────────────────────────────────────────────────────────────────
//  Evaluator — walks the AST and evaluates against rows
// ───────────────────────────────────────────────────────────────────
int resolveInt(Node* node, const StaffRow& row) {
    if (auto* fld = dynamic_cast<FieldNode*>(node))
        return row.intField(fld->label);
    if (auto* num = dynamic_cast<NumberNode*>(node))
        return num->amount;
    throw runtime_error("invalid expression in resolveInt");
}

bool matches(Node* node, const StaffRow& row) {
    auto* bin = dynamic_cast<BinaryNode*>(node);
    if (!bin) throw runtime_error("expression not valid");

    if (bin->oper == "OR")
        return matches(bin->lhs, row) || matches(bin->rhs, row);
    if (bin->oper == "AND")
        return matches(bin->lhs, row) && matches(bin->rhs, row);

    int left  = resolveInt(bin->lhs, row);
    int right = resolveInt(bin->rhs, row);
    if (bin->oper == ">")  return left > right;
    if (bin->oper == "<")  return left < right;
    if (bin->oper == ">=") return left >= right;
    if (bin->oper == "<=") return left <= right;
    if (bin->oper == "=")  return left == right;
    throw runtime_error("invalid operator");
}

void runQuery(Query& q, const vector<StaffRow>& staff) {
    cout << "Query: SELECT " << q.projection << " FROM " << q.source
         << " WHERE <condition>" << endl;
    cout << "Results:" << endl;
    for (const auto& row : staff) {
        if (matches(q.condition, row)) {
            if (q.projection == "name")      cout << "  " << row.textField("name") << endl;
            else if (q.projection == "id")   cout << "  " << row.intField("id") << endl;
            else if (q.projection == "age")  cout << "  " << row.intField("age") << endl;
        }
    }
}

// ───────────────────────────────────────────────────────────────────
int main() {
    vector<StaffRow> staff;
    staff.push_back({"Aarav", 1, 22});
    staff.push_back({"Diya",  2, 22});
    staff.push_back({"Rohan", 3, 28});
    staff.push_back({"Meera", 4, 24});
    staff.push_back({"Kabir", 5, 22});

    string sqlText = "SELECT name FROM employees WHERE id >= 3";

    cout << "=== SQL Query Parser (Piyush Pawan Kumar, 24BCS10296) ===" << endl;
    cout << "Input SQL: " << sqlText << endl << endl;

    Scanner scanner(sqlText);
    vector<Lexeme> lexemes = scanner.scan();

    cout << "Tokens:" << endl;
    for (const auto& lx : lexemes) {
        if (lx.kind != LexKind::EOL)
            cout << "  [" << lx.text << "]" << endl;
    }
    cout << endl;

    GrammarParser parser(lexemes);
    Query q = parser.parseQuery();

    runQuery(q, staff);

    return 0;
}
