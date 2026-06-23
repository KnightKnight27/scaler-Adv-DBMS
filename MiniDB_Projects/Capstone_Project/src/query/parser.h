#pragma once

#include <string>
#include <vector>
#include <optional>

/**
 * @enum CmdType
 * @brief Represents the logical query command categories supported by the REPL parser.
 */
enum class CmdType {
    CREATE_TABLE,
    INSERT,
    SELECT_ALL,     ///< SELECT * FROM table
    SELECT_KEY,     ///< SELECT * FROM table WHERE id = <key>
    SELECT_RANGE,   ///< SELECT * FROM table WHERE id > <key>
    JOIN,           ///< SELECT * FROM t1 JOIN t2 ON t1.id = t2.id
    DELETE_KEY,     ///< DELETE FROM table WHERE id = <key>
    BEGIN,
    COMMIT,
    ABORT,
    SHOW_TABLES,
    HELP,
    QUIT,
    UNKNOWN
};

/**
 * @struct ParsedQuery
 * @brief Struct encapsulating the properties and attributes extracted from a parsed SQL command.
 */
struct ParsedQuery {
    CmdType type = CmdType::UNKNOWN; ///< Parsed command type
    std::string table1;             ///< Primary table identifier
    std::string table2;             ///< Joined secondary table identifier (for JOIN)
    int32_t key = 0;                ///< Key filter values (for WHERE comparisons)
    std::string value;              ///< Raw string payload to insert
    std::string error_msg;          ///< Populated error explanation on parse failures
    bool valid = false;             ///< Verification flag indicating parse validity

    std::string op;                 ///< Relational filter operator (e.g. ">", "=", etc.)
};

/**
 * @class Parser
 * @brief Handles parsing of user command-line strings into ParsedQuery execution plans.
 *
 * Utilizes case-insensitive tokenization and strips formatting characters from payloads.
 */
class Parser {
public:
    /**
     * @brief Parses a raw command-line input string into a structured ParsedQuery representation.
     */
    ParsedQuery parse(const std::string& input) const;

private:
    std::vector<std::string> tokenize(const std::string& input) const;

    std::string toUpper(std::string s) const;

    std::optional<int32_t> parseInt(const std::string& s) const;
};
