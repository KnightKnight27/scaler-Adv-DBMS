// ============================================================================
// Header: Miniature SQL SELECT Processing Subsystem
// Architecture: Shunting-Yard Predicate Evaluation Pipeline
// ============================================================================

#ifndef MINI_SQL_ENGINE_H
#define MINI_SQL_ENGINE_H

#include <string>
#include <vector>
#include <variant>
#include <iosfwd>

namespace mini_sql
{

    // ---------------------------------------------------------------------------
    // Core Relational Data Representation
    // ---------------------------------------------------------------------------

    // A table cell can store either a 64-bit integer or a standard string
    using CellValue = std::variant<long long, std::string>;

    // Represents a single horizontal row of data
    struct Tuple
    {
        std::vector<CellValue> values;
    };

    // Represents an entire in-memory database table
    struct Relation
    {
        std::string table_name;
        std::vector<std::string> headers;
        std::vector<Tuple> records;

        // Helper: Resolves a column header name to its 0-indexed position
        int getColumnIndex(const std::string &columnName) const;
    };

    // ---------------------------------------------------------------------------
    // Lexical Analysis (Tokenizer)
    // ---------------------------------------------------------------------------

    enum class LexemeType
    {
        KeywordSelect,
        KeywordFrom,
        KeywordWhere,
        KeywordOrder,
        KeywordBy,
        KeywordAsc,
        KeywordDesc,
        KeywordLimit,
        LogicalOp,
        TextIdentifier,
        IntegerLiteral,
        StringLiteral,
        ParenOpen,
        ParenClose,
        CommaSeparator,
        StarSymbol,
        StreamEnd
    };

    struct Token
    {
        LexemeType type;
        std::string raw_string;
        long long numeric_value = 0; // Populated only if type == IntegerLiteral
    };

    // Scans a raw SQL string and breaks it down into sequential tokens
    std::vector<Token> tokenizeSQL(const std::string &rawSQL);

    // ---------------------------------------------------------------------------
    // Predicate Compilation (Dijkstra's Shunting-Yard)
    // ---------------------------------------------------------------------------

    // Transforms an Infix token stream into Reverse Polish Notation (RPN)
    std::vector<Token> shuntingYardRPN(const std::vector<Token> &infixTokens);

    // Evaluates a pre-compiled RPN condition stack against a specific row
    bool evaluateRPN(const std::vector<Token> &rpnSequence,
                     const Relation &tableSchema,
                     const Tuple &rowInstance);

    // Utility: Flattens an RPN token vector into a space-separated string
    std::string stringifyRPN(const std::vector<Token> &rpnSequence);

    // ---------------------------------------------------------------------------
    // Syntax Parsing & Query Execution Engine
    // ---------------------------------------------------------------------------

    struct ParsedQuery
    {
        std::vector<std::string> projected_columns; // Empty implies 'SELECT *'
        std::string from_table;
        std::vector<Token> where_rpn_stack; // Compiled postfix condition
        std::string order_column;           // Empty implies no sorting
        bool sort_descending = false;
        long long limit_count = -1; // -1 implies no truncation
    };

    // Parses a verified stream of tokens into an abstract query execution plan
    ParsedQuery compileQuery(const std::string &sqlStatement);

    // Executes a compiled query plan against a target in-memory relation
    Relation executeQuery(const ParsedQuery &plan, const Relation &sourceDatabase);

    // Renders the contents of a relation into a clean ASCII grid
    void printTable(const Relation &table, std::ostream &outputStream);

} // namespace mini_sql

#endif // MINI_SQL_ENGINE_H