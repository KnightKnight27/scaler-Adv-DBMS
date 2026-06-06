#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <stdexcept>

using namespace std;

struct StaffRow {
    string fullName;
    int recordId;
    int years;

    // resolve a column name to its value on this row
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

enum class LexKind {
    KW_SELECT,
    KW_FROM,
    KW_WHERE,
    KW_OR,
    NAME,
    NUMERAL,
    GREATER,
    LESSER,
    EQUAL,
    GREATER_EQ,
    LESSER_EQ,
    PAREN_OPEN,
    PAREN_CLOSE,
    EOL,
    OPERATOR,
    INT_LITERAL,
    KEYWORD
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
        while (cursor < source.size()) {
            if (isspace(source[cursor])) {
                ++cursor;
                continue;
            }

            if (isalpha(source[cursor])) {
                std::string word;

                while (cursor < source.size() &&
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
                else
                    out.push_back({LexKind::NAME, word});
            } else if (isdigit(source[cursor])) {
                std::string digits;
                while (cursor < source.size() &&
                       isdigit(source[cursor])) {
                    digits += source[cursor++];
                }

                out.push_back({LexKind::NUMERAL, digits});
            } else if (source[cursor] == '>') {
                if (cursor + 1 < source.size() && source[cursor + 1] == '=') {
                    out.push_back({LexKind::GREATER_EQ, ">="});
                    cursor += 2;
                } else {
                    out.push_back({LexKind::GREATER, ">"});
                    ++cursor;
                }
            } else if (source[cursor] == '<') {
                if (cursor + 1 < source.size() && source[cursor + 1] == '=') {
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
                ++cursor;
            }
        }
        out.push_back({LexKind::EOL, ""});
        return out;
    }
};

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
        Node* lhs = parseAtom();
        while (lexemes[idx].kind == LexKind::KW_OR) {
            expect(LexKind::KW_OR);
            Node* rhs = parseAtom();
            lhs = new BinaryNode("OR", lhs, rhs);
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
        } else {
            throw runtime_error("Expected >, <, >= or <=");
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

// Resolve a node to an int: a field reads from the row, a number returns
// its own value. Lets matches() treat both sides the same way.
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

    // both sides resolved through resolveInt — works for field vs number,
    // number vs field, or even field vs field
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
    for (const auto& row : staff) {
        if (matches(q.condition, row)) {
            if (q.projection == "name")      cout << row.textField("name") << endl;
            else if (q.projection == "id")   cout << row.intField("id") << endl;
            else if (q.projection == "age")  cout << row.intField("age") << endl;
        }
    }
}

int main() {
    vector<StaffRow> staff;
    staff.push_back({"Aarav", 1, 22});
    staff.push_back({"Diya",  2, 22});
    staff.push_back({"Rohan", 3, 28});
    staff.push_back({"Meera", 4, 24});
    staff.push_back({"Kabir", 5, 22});

    string sqlText = "SELECT name from employees where id >= 3";

    Scanner scanner(sqlText);
    vector<Lexeme> lexemes = scanner.scan();

    GrammarParser parser(lexemes);
    Query q = parser.parseQuery();

    // for (auto lx : lexemes) {
    //     cout << lx.text << endl;
    // }

    runQuery(q, staff);

    return 0;
}
