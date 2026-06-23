#pragma once

#include <string>
#include <vector>
#include <optional>

// ─── Command types ────────────────────────────────────────────────────────────
// Every SQL-like command the REPL understands maps to one of these.

enum class CmdType {
    CREATE_TABLE,
    INSERT,
    SELECT_ALL,     // SELECT * FROM table
    SELECT_KEY,     // SELECT * FROM table WHERE id = <key>
    SELECT_RANGE,   // SELECT * FROM table WHERE id > <key>
    JOIN,           // SELECT * FROM t1 JOIN t2 ON t1.id = t2.id
    DELETE_KEY,     // DELETE FROM table WHERE id = <key>
    BEGIN,
    COMMIT,
    ABORT,
    SHOW_TABLES,
    HELP,
    QUIT,
    UNKNOWN
};

// ─── ParsedQuery ──────────────────────────────────────────────────────────────
// Result of parsing one user command. The executor reads this struct.

struct ParsedQuery {
    CmdType     type = CmdType::UNKNOWN;
    std::string table1;       // primary table name
    std::string table2;       // secondary table name (for JOIN)
    int32_t     key = 0;      // key value for WHERE id = <key>
    std::string value;        // record value string for INSERT
    std::string error_msg;    // filled if parsing fails
    bool        valid = false;

    // For SELECT_RANGE: the condition operator ('>', '<', '>=', '<=', '=')
    std::string op;
};

// ─── Parser ───────────────────────────────────────────────────────────────────
// Parses a single command string into a ParsedQuery.
//
// Supported grammar:
//   CREATE TABLE <name>
//   INSERT INTO <table> VALUES (<key>, <value>)
//   SELECT * FROM <table>
//   SELECT * FROM <table> WHERE id = <key>
//   SELECT * FROM <table> WHERE id > <key>
//   SELECT * FROM <t1> JOIN <t2> ON t1.id = t2.id
//   DELETE FROM <table> WHERE id = <key>
//   BEGIN
//   COMMIT
//   ABORT
//   SHOW TABLES
//   HELP
//   QUIT
//
// The parser is case-insensitive for keywords.

class Parser {
public:
    // Parse a command line into a ParsedQuery.
    // If the command is unrecognized or malformed, returns a query with valid=false.
    ParsedQuery parse(const std::string& input) const;

private:
    // Split input into whitespace-delimited tokens, lowercasing keyword tokens.
    std::vector<std::string> tokenize(const std::string& input) const;

    // Convert a string to uppercase
    std::string toUpper(std::string s) const;

    // Try to parse an integer from a string. Returns nullopt on failure.
    std::optional<int32_t> parseInt(const std::string& s) const;
};
