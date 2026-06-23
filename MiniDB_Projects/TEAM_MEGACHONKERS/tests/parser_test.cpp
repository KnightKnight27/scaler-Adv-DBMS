#include <gtest/gtest.h>
#include <memory>

#include "parser/lexer.h"
#include "parser/parser.h"
#include "parser/ast.h"
#include "parser/expression.h"
#include "catalog/schema.h"
#include "common/types.h"

using namespace minidb;

// ---------------------------------------------------------------------------
// Lexer
// ---------------------------------------------------------------------------
TEST(LexerTest, TokenizesKeywordsLiteralsAndOperators) {
    Lexer lexer("SELECT * FROM users WHERE age >= 30 AND name = 'Bob';");
    auto tokens = lexer.Tokenize();

    ASSERT_GE(tokens.size(), 12u);
    EXPECT_EQ(tokens[0].type, TokenType::KW_SELECT);
    EXPECT_EQ(tokens[1].type, TokenType::STAR);
    EXPECT_EQ(tokens[2].type, TokenType::KW_FROM);
    EXPECT_EQ(tokens[3].type, TokenType::IDENTIFIER);
    EXPECT_EQ(tokens[3].lexeme, "users");
    EXPECT_EQ(tokens[4].type, TokenType::KW_WHERE);
    EXPECT_EQ(tokens[6].type, TokenType::GREATER_EQUAL);
    EXPECT_EQ(tokens[7].type, TokenType::INTEGER_LITERAL);
    EXPECT_EQ(tokens[7].lexeme, "30");
    EXPECT_EQ(tokens[8].type, TokenType::KW_AND);
    EXPECT_EQ(tokens.back().type, TokenType::END_OF_FILE);
}

TEST(LexerTest, StringLiteralStripsQuotesAndKeepsSpaces) {
    Lexer lexer("INSERT INTO t VALUES ('hello world');");
    auto tokens = lexer.Tokenize();
    bool found = false;
    for (const auto& t : tokens) {
        if (t.type == TokenType::STRING_LITERAL) {
            EXPECT_EQ(t.lexeme, "hello world");
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

// ---------------------------------------------------------------------------
// Rich expression trees: parse a WHERE clause and evaluate it against a row.
// (Column refs resolve by name against the schema since no planner bound them.)
// ---------------------------------------------------------------------------
class ExpressionEvalTest : public ::testing::Test {
protected:
    Schema schema{{Column("id", TypeId::INTEGER, 4),
                   Column("age", TypeId::INTEGER, 4),
                   Column("role", TypeId::VARCHAR, 255)}};

    ExprPtr ParseWhere(const std::string& sql) {
        Parser parser(sql);
        StmtPtr stmt = parser.Parse();
        auto* sel = dynamic_cast<SelectStatement*>(stmt.get());
        EXPECT_NE(sel, nullptr);
        return sel->where ? sel->where->Clone() : nullptr;
    }
};

TEST_F(ExpressionEvalTest, NumericComparisonNotLexicographic) {
    auto pred = ParseWhere("SELECT * FROM t WHERE age > 9");
    ASSERT_NE(pred, nullptr);
    // "10" > "9" must be true numerically (lexicographically it would be false).
    EXPECT_TRUE(pred->EvalBool(Row{{"1", "10", "eng"}}, &schema));
    EXPECT_FALSE(pred->EvalBool(Row{{"1", "5", "eng"}}, &schema));
}

TEST_F(ExpressionEvalTest, AndOrPrecedenceAndGrouping) {
    // AND binds tighter than OR: matches when role=eng OR (id=1).
    auto pred = ParseWhere("SELECT * FROM t WHERE role = 'mgr' AND age > 40 OR id = 1");
    ASSERT_NE(pred, nullptr);
    EXPECT_TRUE(pred->EvalBool(Row{{"1", "20", "eng"}}, &schema));   // id=1 branch
    EXPECT_TRUE(pred->EvalBool(Row{{"7", "50", "mgr"}}, &schema));   // mgr AND >40
    EXPECT_FALSE(pred->EvalBool(Row{{"7", "20", "mgr"}}, &schema));  // mgr but age<=40

    auto grouped = ParseWhere("SELECT * FROM t WHERE role = 'mgr' AND (age > 40 OR id = 1)");
    ASSERT_NE(grouped, nullptr);
    EXPECT_TRUE(grouped->EvalBool(Row{{"1", "20", "mgr"}}, &schema));  // mgr AND id=1
    EXPECT_FALSE(grouped->EvalBool(Row{{"1", "20", "eng"}}, &schema)); // not mgr
}

TEST_F(ExpressionEvalTest, InequalityOperators) {
    EXPECT_TRUE(ParseWhere("SELECT * FROM t WHERE role != 'eng'")
                    ->EvalBool(Row{{"1", "1", "mgr"}}, &schema));
    EXPECT_TRUE(ParseWhere("SELECT * FROM t WHERE age <= 30")
                    ->EvalBool(Row{{"1", "30", "eng"}}, &schema));
}

// ---------------------------------------------------------------------------
// Parser productions
// ---------------------------------------------------------------------------
TEST(ParserTest, CreateTableResolvesTypes) {
    Parser parser("CREATE TABLE users (id INT, name VARCHAR);");
    StmtPtr stmt = parser.Parse();
    auto* create = dynamic_cast<CreateTableStatement*>(stmt.get());
    ASSERT_NE(create, nullptr);
    EXPECT_EQ(create->table_name, "users");
    ASSERT_EQ(create->columns.size(), 2u);
    EXPECT_EQ(create->columns[0].name, "id");
    EXPECT_EQ(create->columns[0].type, TypeId::INTEGER);
    EXPECT_EQ(create->columns[1].type, TypeId::VARCHAR);
}

TEST(ParserTest, MultiRowInsert) {
    Parser parser("INSERT INTO t VALUES (1, 'a'), (2, 'b'), (3, 'c');");
    StmtPtr stmt = parser.Parse();
    auto* ins = dynamic_cast<InsertStatement*>(stmt.get());
    ASSERT_NE(ins, nullptr);
    ASSERT_EQ(ins->rows.size(), 3u);
    EXPECT_EQ(ins->rows[1][0], "2");
    EXPECT_EQ(ins->rows[2][1], "c");
}

TEST(ParserTest, SelectProjectionList) {
    Parser parser("SELECT id, users.name FROM users;");
    StmtPtr stmt = parser.Parse();
    auto* sel = dynamic_cast<SelectStatement*>(stmt.get());
    ASSERT_NE(sel, nullptr);
    EXPECT_FALSE(sel->select_star);
    ASSERT_EQ(sel->select_list.size(), 2u);
    EXPECT_EQ(sel->select_list[0].column, "id");
    EXPECT_EQ(sel->select_list[1].table, "users");
    EXPECT_EQ(sel->select_list[1].column, "name");
}

TEST(ParserTest, JoinClauseParsed) {
    Parser parser("SELECT * FROM users JOIN orders ON users.id = orders.user_id;");
    StmtPtr stmt = parser.Parse();
    auto* sel = dynamic_cast<SelectStatement*>(stmt.get());
    ASSERT_NE(sel, nullptr);
    ASSERT_TRUE(sel->join.present);
    EXPECT_EQ(sel->join.right_table, "orders");
    EXPECT_EQ(sel->join.left_key.table, "users");
    EXPECT_EQ(sel->join.left_key.column, "id");
    EXPECT_EQ(sel->join.right_key.table, "orders");
    EXPECT_EQ(sel->join.right_key.column, "user_id");
}

TEST(ParserTest, DeleteWithWhere) {
    Parser parser("DELETE FROM items WHERE id = 2;");
    StmtPtr stmt = parser.Parse();
    auto* del = dynamic_cast<DeleteStatement*>(stmt.get());
    ASSERT_NE(del, nullptr);
    EXPECT_EQ(del->table_name, "items");
    ASSERT_NE(del->where, nullptr);
}

TEST(ParserTest, CreateIndex) {
    Parser parser("CREATE INDEX idx_id ON users (id);");
    StmtPtr stmt = parser.Parse();
    auto* idx = dynamic_cast<CreateIndexStatement*>(stmt.get());
    ASSERT_NE(idx, nullptr);
    EXPECT_EQ(idx->index_name, "idx_id");
    EXPECT_EQ(idx->table_name, "users");
    EXPECT_EQ(idx->column_name, "id");
}

TEST(ParserTest, TransactionCommands) {
    EXPECT_EQ(Parser("BEGIN;").Parse()->type, StatementType::BEGIN_TXN);
    EXPECT_EQ(Parser("COMMIT;").Parse()->type, StatementType::COMMIT_TXN);
    EXPECT_EQ(Parser("ROLLBACK;").Parse()->type, StatementType::ROLLBACK_TXN);
}

TEST(ParserTest, SyntaxErrorBecomesInvalidStatement) {
    Parser parser("SELECT FROM;"); // missing projection target
    StmtPtr stmt = parser.Parse();
    auto* invalid = dynamic_cast<InvalidStatement*>(stmt.get());
    ASSERT_NE(invalid, nullptr);
    EXPECT_FALSE(invalid->error.empty());
}

TEST(ParserTest, CaseInsensitiveKeywords) {
    Parser parser("select * from users where id = 1;");
    StmtPtr stmt = parser.Parse();
    EXPECT_EQ(stmt->type, StatementType::SELECT);
}
