#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <stdexcept>

using namespace std;

struct Student {
    string studentName;
    int studentId;
    int studentAge;

    int fetchNumericField(const string& field) const {
        if (field == "id")  return studentId;
        if (field == "age") return studentAge;

        throw runtime_error("Unrecognized numeric field: " + field);
    }

    string fetchTextField(const string& field) const {
        if (field == "name") return studentName;

        throw runtime_error("Unrecognized text field: " + field);
    }
};

enum class LexemeType {
    KW_SELECT,
    KW_FROM,
    KW_WHERE,
    KW_OR,

    NAME,
    NUMERIC,

    OP_GT,
    OP_LT,
    OP_EQ,
    OP_GTE,
    OP_LTE,

    PAREN_OPEN,
    PAREN_CLOSE,

    INPUT_END
};

struct Lexeme {
    LexemeType kind;
    string text;
};

class Scanner {
private:
    string inputQuery;

public:
    Scanner(const string& input) : inputQuery(input) {}

    vector<Lexeme> scan() {
        vector<Lexeme> lexemes;
        int idx = 0;

        while (idx < inputQuery.size()) {

            if (isspace(inputQuery[idx])) {
                idx++;
                continue;
            }

            if (isalpha(inputQuery[idx])) {

                string fragment;

                while (idx < inputQuery.size() &&
                       (isalnum(inputQuery[idx]) ||
                        inputQuery[idx] == '_')) {

                    fragment += inputQuery[idx++];
                }

                string normalized = fragment;
                transform(
                    normalized.begin(),
                    normalized.end(),
                    normalized.begin(),
                    ::toupper
                );

                if (normalized == "SELECT")
                    lexemes.push_back({LexemeType::KW_SELECT, fragment});
                else if (normalized == "FROM")
                    lexemes.push_back({LexemeType::KW_FROM, fragment});
                else if (normalized == "WHERE")
                    lexemes.push_back({LexemeType::KW_WHERE, fragment});
                else if (normalized == "OR")
                    lexemes.push_back({LexemeType::KW_OR, fragment});
                else
                    lexemes.push_back({LexemeType::NAME, fragment});

                continue;
            }

            if (isdigit(inputQuery[idx])) {

                string numStr;

                while (idx < inputQuery.size() &&
                       isdigit(inputQuery[idx])) {

                    numStr += inputQuery[idx++];
                }

                lexemes.push_back({LexemeType::NUMERIC, numStr});
                continue;
            }

            if (inputQuery[idx] == '>') {

                if (idx + 1 < inputQuery.size() &&
                    inputQuery[idx + 1] == '=') {

                    lexemes.push_back({LexemeType::OP_GTE, ">="});
                    idx += 2;
                }
                else {
                    lexemes.push_back({LexemeType::OP_GT, ">"});
                    idx++;
                }

                continue;
            }

            if (inputQuery[idx] == '<') {

                if (idx + 1 < inputQuery.size() &&
                    inputQuery[idx + 1] == '=') {

                    lexemes.push_back({LexemeType::OP_LTE, "<="});
                    idx += 2;
                }
                else {
                    lexemes.push_back({LexemeType::OP_LT, "<"});
                    idx++;
                }

                continue;
            }

            if (inputQuery[idx] == '=') {
                lexemes.push_back({LexemeType::OP_EQ, "="});
                idx++;
                continue;
            }

            if (inputQuery[idx] == '(') {
                lexemes.push_back({LexemeType::PAREN_OPEN, "("});
                idx++;
                continue;
            }

            if (inputQuery[idx] == ')') {
                lexemes.push_back({LexemeType::PAREN_CLOSE, ")"});
                idx++;
                continue;
            }

            idx++;
        }

        lexemes.push_back({LexemeType::INPUT_END, ""});
        return lexemes;
    }
};

struct ASTNode {
    virtual ~ASTNode() = default;
};

struct NumberNode : ASTNode {
    int val;

    NumberNode(int v)
        : val(v) {}
};

struct FieldNode : ASTNode {
    string fieldName;

    FieldNode(const string& f)
        : fieldName(f) {}
};

struct BinaryOpNode : ASTNode {
    string op;
    ASTNode* leftChild;
    ASTNode* rightChild;

    BinaryOpNode(
        const string& oper,
        ASTNode* lhs,
        ASTNode* rhs
    )
        : op(oper),
          leftChild(lhs),
          rightChild(rhs) {}
};

struct QueryDescriptor {
    string targetColumn;
    string sourceTable;
    ASTNode* whereClause;
};

class QueryParser {
private:
    vector<Lexeme> lexemes;
    int position = 0;

    Lexeme expect(LexemeType expectedKind) {
        if (lexemes[position].kind != expectedKind)
            throw runtime_error("Unexpected lexeme encountered");

        return lexemes[position++];
    }

    ASTNode* handleComparison() {

        string col =
            expect(LexemeType::NAME).text;

        ASTNode* lhs =
            new FieldNode(col);

        string oper;

        if (lexemes[position].kind == LexemeType::OP_GTE) {
            oper = ">=";
            expect(LexemeType::OP_GTE);
        }
        else if (lexemes[position].kind == LexemeType::OP_LTE) {
            oper = "<=";
            expect(LexemeType::OP_LTE);
        }
        else if (lexemes[position].kind == LexemeType::OP_GT) {
            oper = ">";
            expect(LexemeType::OP_GT);
        }
        else if (lexemes[position].kind == LexemeType::OP_LT) {
            oper = "<";
            expect(LexemeType::OP_LT);
        }
        else {
            throw runtime_error(
                "Expected a comparison operator"
            );
        }

        int num =
            stoi(expect(LexemeType::NUMERIC).text);

        ASTNode* rhs =
            new NumberNode(num);

        return new BinaryOpNode(oper, lhs, rhs);
    }

    ASTNode* handleAtom() {

        if (lexemes[position].kind ==
            LexemeType::PAREN_OPEN) {

            expect(LexemeType::PAREN_OPEN);

            ASTNode* nested =
                handleDisjunction();

            expect(LexemeType::PAREN_CLOSE);

            return nested;
        }

        return handleComparison();
    }

    ASTNode* handleDisjunction() {

        ASTNode* lhs =
            handleAtom();

        while (lexemes[position].kind ==
               LexemeType::KW_OR) {

            expect(LexemeType::KW_OR);

            ASTNode* rhs =
                handleAtom();

            lhs = new BinaryOpNode(
                "OR",
                lhs,
                rhs
            );
        }

        return lhs;
    }

public:
    QueryParser(const vector<Lexeme>& lex)
        : lexemes(lex) {}

    QueryDescriptor buildQuery() {

        expect(LexemeType::KW_SELECT);

        string col =
            expect(LexemeType::NAME).text;

        expect(LexemeType::KW_FROM);

        string tbl =
            expect(LexemeType::NAME).text;

        expect(LexemeType::KW_WHERE);

        ASTNode* filter =
            handleDisjunction();

        return {
            col,
            tbl,
            filter
        };
    }
};

int resolveNumeric(
    ASTNode* node,
    const Student& record
) {

    if (auto* field =
            dynamic_cast<FieldNode*>(node)) {

        return record.fetchNumericField(
            field->fieldName
        );
    }

    if (auto* num =
            dynamic_cast<NumberNode*>(node)) {

        return num->val;
    }

    throw runtime_error(
        "Cannot resolve numeric value"
    );
}

bool resolveCondition(
    ASTNode* node,
    const Student& record
) {

    auto* binOp =
        dynamic_cast<BinaryOpNode*>(node);

    if (!binOp)
        throw runtime_error(
            "Malformed condition node"
        );

    if (binOp->op == "OR") {

        return resolveCondition(
                   binOp->leftChild,
                   record
               ) ||
               resolveCondition(
                   binOp->rightChild,
                   record
               );
    }

    int left =
        resolveNumeric(
            binOp->leftChild,
            record
        );

    int right =
        resolveNumeric(
            binOp->rightChild,
            record
        );

    if (binOp->op == ">")
        return left > right;

    if (binOp->op == "<")
        return left < right;

    if (binOp->op == ">=")
        return left >= right;

    if (binOp->op == "<=")
        return left <= right;

    if (binOp->op == "=")
        return left == right;

    throw runtime_error(
        "Unrecognized operator"
    );
}

void runQuery(
    const QueryDescriptor& descriptor,
    const vector<Student>& records
) {

    for (const auto& record : records) {

        if (!resolveCondition(
                descriptor.whereClause,
                record))
            continue;

        if (descriptor.targetColumn == "name")
            cout
                << record.fetchTextField("name")
                << endl;

        else if (descriptor.targetColumn == "id")
            cout
                << record.fetchNumericField("id")
                << endl;

        else if (descriptor.targetColumn == "age")
            cout
                << record.fetchNumericField("age")
                << endl;
    }
}

int main() {

    vector<Student> records = {
        {"Arjun", 1, 21},
        {"Priya", 2, 23},
        {"Vikram", 3, 27},
        {"Sneha", 4, 25},
        {"Rahul", 5, 21}
    };

    string sql =
        "SELECT name FROM students "
        "WHERE id <= 3";

    Scanner scanner(sql);

    vector<Lexeme> lexemes =
        scanner.scan();

    QueryParser parser(lexemes);

    QueryDescriptor descriptor =
        parser.buildQuery();

    runQuery(
        descriptor,
        records
    );

    return 0;
}
