// ============================================================================
// Implementation: SQL Processing & Execution Engine
// ============================================================================

#include "sql_engine.h"

#include <algorithm>
#include <cctype>
#include <ostream>
#include <stack>
#include <stdexcept>
#include <string>

namespace mini_sql
{

    namespace
    {

        // Helper: Convert string to uppercase for case-insensitive keyword matching
        std::string toUpperString(std::string input)
        {
            for (char &ch : input)
            {
                ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            }
            return input;
        }

        // Helpers for identifier parsing
        bool isValidAlpha(char ch)
        {
            return std::isalpha(static_cast<unsigned char>(ch)) || ch == '_';
        }
        bool isValidAlnum(char ch)
        {
            return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
        }

        // Defines operator precedence and associativity
        struct OpMetadata
        {
            int precedence;
            bool is_unary_op;
            bool right_associative;
        };

        OpMetadata getOpMetadata(const std::string &opSymbol)
        {
            if (opSymbol == "=" || opSymbol == "!=" ||
                opSymbol == "<" || opSymbol == "<=" ||
                opSymbol == ">" || opSymbol == ">=")
                return {4, false, false};

            if (opSymbol == "NOT")
                return {3, true, true};
            if (opSymbol == "AND")
                return {2, false, false};
            if (opSymbol == "OR")
                return {1, false, false};

            return {0, false, false}; // Unknown
        }

        // Determines if a cell represents a logical 'true'
        bool isTruthy(const CellValue &val)
        {
            if (std::holds_alternative<long long>(val))
            {
                return std::get<long long>(val) != 0;
            }
            return !std::get<std::string>(val).empty();
        }

        // Compares two cell values. Returns -1 (left < right), 1 (left > right), or 0 (equal).
        int compareCells(const CellValue &leftVal, const CellValue &rightVal, bool &typesMatched)
        {
            typesMatched = true;

            if (std::holds_alternative<long long>(leftVal) && std::holds_alternative<long long>(rightVal))
            {
                long long a = std::get<long long>(leftVal);
                long long b = std::get<long long>(rightVal);
                return (a < b) ? -1 : (a > b) ? 1
                                              : 0;
            }

            if (std::holds_alternative<std::string>(leftVal) && std::holds_alternative<std::string>(rightVal))
            {
                const std::string &a = std::get<std::string>(leftVal);
                const std::string &b = std::get<std::string>(rightVal);
                return (a < b) ? -1 : (a > b) ? 1
                                              : 0;
            }

            typesMatched = false;
            return 0;
        }

        // Extracts the actual data value from a token (or fetches it from the current row)
        CellValue resolveCellValue(const Token &tok, const Relation &dbContext, const Tuple &currentRow)
        {
            if (tok.type == LexemeType::IntegerLiteral)
            {
                return CellValue{tok.numeric_value};
            }
            if (tok.type == LexemeType::StringLiteral)
            {
                return CellValue{tok.raw_string};
            }
            if (tok.type == LexemeType::TextIdentifier)
            {
                int colIndex = dbContext.getColumnIndex(tok.raw_string);
                if (colIndex < 0)
                {
                    throw std::runtime_error("Column reference error: " + tok.raw_string);
                }
                return currentRow.values[static_cast<std::size_t>(colIndex)];
            }
            throw std::runtime_error("Cannot extract value from token: " + tok.raw_string);
        }

    } // anonymous namespace

    int Relation::getColumnIndex(const std::string &columnName) const
    {
        for (std::size_t i = 0; i < headers.size(); ++i)
        {
            if (headers[i] == columnName)
                return static_cast<int>(i);
        }
        return -1;
    }

    std::vector<Token> tokenizeSQL(const std::string &rawSQL)
    {
        std::vector<Token> outStream;
        const std::size_t totalLen = rawSQL.length();
        std::size_t cursor = 0;

        while (cursor < totalLen)
        {
            char currentCh = rawSQL[cursor];

            // Skip whitespaces
            if (std::isspace(static_cast<unsigned char>(currentCh)))
            {
                ++cursor;
                continue;
            }

            // Parse Identifiers and Keywords
            if (isValidAlpha(currentCh))
            {
                std::size_t startPos = cursor;
                while (cursor < totalLen && isValidAlnum(rawSQL[cursor]))
                {
                    ++cursor;
                }
                std::string extracted = rawSQL.substr(startPos, cursor - startPos);
                std::string upperWord = toUpperString(extracted);

                if (upperWord == "SELECT")
                    outStream.push_back({LexemeType::KeywordSelect, upperWord, 0});
                else if (upperWord == "FROM")
                    outStream.push_back({LexemeType::KeywordFrom, upperWord, 0});
                else if (upperWord == "WHERE")
                    outStream.push_back({LexemeType::KeywordWhere, upperWord, 0});
                else if (upperWord == "ORDER")
                    outStream.push_back({LexemeType::KeywordOrder, upperWord, 0});
                else if (upperWord == "BY")
                    outStream.push_back({LexemeType::KeywordBy, upperWord, 0});
                else if (upperWord == "ASC")
                    outStream.push_back({LexemeType::KeywordAsc, upperWord, 0});
                else if (upperWord == "DESC")
                    outStream.push_back({LexemeType::KeywordDesc, upperWord, 0});
                else if (upperWord == "LIMIT")
                    outStream.push_back({LexemeType::KeywordLimit, upperWord, 0});
                else if (upperWord == "AND" || upperWord == "OR" || upperWord == "NOT")
                {
                    outStream.push_back({LexemeType::LogicalOp, upperWord, 0});
                }
                else
                {
                    outStream.push_back({LexemeType::TextIdentifier, extracted, 0});
                }
                continue;
            }

            // Parse Integer Literals
            if (std::isdigit(static_cast<unsigned char>(currentCh)))
            {
                std::size_t startPos = cursor;
                while (cursor < totalLen && std::isdigit(static_cast<unsigned char>(rawSQL[cursor])))
                {
                    ++cursor;
                }
                Token numTok{LexemeType::IntegerLiteral, rawSQL.substr(startPos, cursor - startPos), 0};
                numTok.numeric_value = std::stoll(numTok.raw_string);
                outStream.push_back(numTok);
                continue;
            }

            // Parse String Literals (enclosed in single quotes)
            if (currentCh == '\'')
            {
                std::string buffer;
                ++cursor;
                while (cursor < totalLen)
                {
                    if (rawSQL[cursor] == '\'')
                    {
                        if (cursor + 1 < totalLen && rawSQL[cursor + 1] == '\'')
                        {
                            buffer += '\''; // Escaped quote
                            cursor += 2;
                            continue;
                        }
                        ++cursor;
                        break;
                    }
                    buffer += rawSQL[cursor++];
                }
                outStream.push_back({LexemeType::StringLiteral, buffer, 0});
                continue;
            }

            // Parse Punctuation and Operators
            if (currentCh == '(')
            {
                outStream.push_back({LexemeType::ParenOpen, "("});
                ++cursor;
                continue;
            }
            if (currentCh == ')')
            {
                outStream.push_back({LexemeType::ParenClose, ")"});
                ++cursor;
                continue;
            }
            if (currentCh == ',')
            {
                outStream.push_back({LexemeType::CommaSeparator, ","});
                ++cursor;
                continue;
            }
            if (currentCh == '*')
            {
                outStream.push_back({LexemeType::StarSymbol, "*"});
                ++cursor;
                continue;
            }
            if (currentCh == '=')
            {
                outStream.push_back({LexemeType::LogicalOp, "="});
                ++cursor;
                continue;
            }

            if (currentCh == '!')
            {
                if (cursor + 1 < totalLen && rawSQL[cursor + 1] == '=')
                {
                    outStream.push_back({LexemeType::LogicalOp, "!="});
                    cursor += 2;
                    continue;
                }
                throw std::runtime_error("Malformed operator starting with '!'");
            }

            if (currentCh == '<' || currentCh == '>')
            {
                std::string opStr(1, currentCh);
                if (cursor + 1 < totalLen && rawSQL[cursor + 1] == '=')
                {
                    opStr += "=";
                    cursor += 2;
                }
                else
                {
                    ++cursor;
                }
                outStream.push_back({LexemeType::LogicalOp, opStr, 0});
                continue;
            }

            throw std::runtime_error(std::string("Unrecognized character in query: ") + currentCh);
        }

        outStream.push_back({LexemeType::StreamEnd, "", 0});
        return outStream;
    }

    std::vector<Token> shuntingYardRPN(const std::vector<Token> &infixTokens)
    {
        std::vector<Token> outputQueue;
        std::stack<Token> opStack;

        for (const auto &tok : infixTokens)
        {
            if (tok.type == LexemeType::TextIdentifier ||
                tok.type == LexemeType::IntegerLiteral ||
                tok.type == LexemeType::StringLiteral)
            {
                outputQueue.push_back(tok);
            }
            else if (tok.type == LexemeType::ParenOpen)
            {
                opStack.push(tok);
            }
            else if (tok.type == LexemeType::ParenClose)
            {
                while (!opStack.empty() && opStack.top().type != LexemeType::ParenOpen)
                {
                    outputQueue.push_back(opStack.top());
                    opStack.pop();
                }
                if (opStack.empty())
                    throw std::runtime_error("Unmatched ')' parenthesis.");
                opStack.pop(); // Remove the '('
            }
            else if (tok.type == LexemeType::LogicalOp)
            {
                OpMetadata currentMeta = getOpMetadata(tok.raw_string);
                while (!opStack.empty() && opStack.top().type == LexemeType::LogicalOp)
                {
                    OpMetadata stackMeta = getOpMetadata(opStack.top().raw_string);

                    bool shouldPop = (stackMeta.precedence > currentMeta.precedence) ||
                                     (stackMeta.precedence == currentMeta.precedence && !currentMeta.right_associative);
                    if (!shouldPop)
                        break;

                    outputQueue.push_back(opStack.top());
                    opStack.pop();
                }
                opStack.push(tok);
            }
            else if (tok.type == LexemeType::StreamEnd)
            {
                break;
            }
            else
            {
                throw std::runtime_error("Invalid token found in WHERE clause: " + tok.raw_string);
            }
        }

        while (!opStack.empty())
        {
            if (opStack.top().type == LexemeType::ParenOpen)
            {
                throw std::runtime_error("Unmatched '(' parenthesis.");
            }
            outputQueue.push_back(opStack.top());
            opStack.pop();
        }

        return outputQueue;
    }

    bool evaluateRPN(const std::vector<Token> &rpnSequence, const Relation &tableSchema, const Tuple &rowInstance)
    {
        std::stack<CellValue> evalStack;

        for (const auto &tok : rpnSequence)
        {
            if (tok.type != LexemeType::LogicalOp)
            {
                evalStack.push(resolveCellValue(tok, tableSchema, rowInstance));
                continue;
            }

            OpMetadata meta = getOpMetadata(tok.raw_string);

            if (meta.is_unary_op)
            {
                if (evalStack.empty())
                    throw std::runtime_error("Unary NOT missing operand.");
                bool active = isTruthy(evalStack.top());
                evalStack.pop();
                evalStack.push(CellValue{static_cast<long long>(!active)});
                continue;
            }

            if (evalStack.size() < 2)
                throw std::runtime_error("Binary operator missing operands: " + tok.raw_string);

            CellValue rightOperand = evalStack.top();
            evalStack.pop();
            CellValue leftOperand = evalStack.top();
            evalStack.pop();

            if (tok.raw_string == "AND")
            {
                evalStack.push(CellValue{static_cast<long long>(isTruthy(leftOperand) && isTruthy(rightOperand))});
                continue;
            }
            if (tok.raw_string == "OR")
            {
                evalStack.push(CellValue{static_cast<long long>(isTruthy(leftOperand) || isTruthy(rightOperand))});
                continue;
            }

            bool matchType = true;
            int compareRes = compareCells(leftOperand, rightOperand, matchType);
            bool conditionMet = false;

            if (matchType)
            {
                if (tok.raw_string == "=")
                    conditionMet = (compareRes == 0);
                else if (tok.raw_string == "!=")
                    conditionMet = (compareRes != 0);
                else if (tok.raw_string == "<")
                    conditionMet = (compareRes < 0);
                else if (tok.raw_string == "<=")
                    conditionMet = (compareRes <= 0);
                else if (tok.raw_string == ">")
                    conditionMet = (compareRes > 0);
                else if (tok.raw_string == ">=")
                    conditionMet = (compareRes >= 0);
            }

            evalStack.push(CellValue{static_cast<long long>(conditionMet)});
        }

        if (evalStack.empty())
            return true; // Empty WHERE clause is always true
        return isTruthy(evalStack.top());
    }

    std::string stringifyRPN(const std::vector<Token> &rpnSequence)
    {
        std::string flatString;
        for (const auto &tok : rpnSequence)
        {
            if (!flatString.empty())
                flatString += " ";
            flatString += (tok.type == LexemeType::StringLiteral) ? ("'" + tok.raw_string + "'") : tok.raw_string;
        }
        return flatString;
    }

    ParsedQuery compileQuery(const std::string &sqlStatement)
    {
        std::vector<Token> tokenStream = tokenizeSQL(sqlStatement);
        ParsedQuery plan;
        std::size_t idx = 0;

        auto verifyNext = [&](LexemeType expectedType, const std::string &err)
        {
            if (tokenStream[idx].type != expectedType)
                throw std::runtime_error("Parsing failure. Expected: " + err);
        };

        // 1. SELECT clause
        verifyNext(LexemeType::KeywordSelect, "SELECT");
        ++idx;

        if (tokenStream[idx].type == LexemeType::StarSymbol)
        {
            ++idx;
        }
        else
        {
            while (true)
            {
                verifyNext(LexemeType::TextIdentifier, "Target Column Name");
                plan.projected_columns.push_back(tokenStream[idx].raw_string);
                ++idx;
                if (tokenStream[idx].type == LexemeType::CommaSeparator)
                {
                    ++idx;
                    continue;
                }
                break;
            }
        }

        // 2. FROM clause
        verifyNext(LexemeType::KeywordFrom, "FROM");
        ++idx;
        verifyNext(LexemeType::TextIdentifier, "Table Name");
        plan.from_table = tokenStream[idx].raw_string;
        ++idx;

        // 3. WHERE clause
        if (tokenStream[idx].type == LexemeType::KeywordWhere)
        {
            ++idx;
            std::vector<Token> infixTokens;
            while (tokenStream[idx].type != LexemeType::KeywordOrder &&
                   tokenStream[idx].type != LexemeType::KeywordLimit &&
                   tokenStream[idx].type != LexemeType::StreamEnd)
            {
                infixTokens.push_back(tokenStream[idx++]);
            }
            plan.where_rpn_stack = shuntingYardRPN(infixTokens);
        }

        // 4. ORDER BY clause
        if (tokenStream[idx].type == LexemeType::KeywordOrder)
        {
            ++idx;
            verifyNext(LexemeType::KeywordBy, "BY");
            ++idx;
            verifyNext(LexemeType::TextIdentifier, "Sort Column Name");
            plan.order_column = tokenStream[idx].raw_string;
            ++idx;
            if (tokenStream[idx].type == LexemeType::KeywordAsc)
            {
                ++idx;
            }
            else if (tokenStream[idx].type == LexemeType::KeywordDesc)
            {
                plan.sort_descending = true;
                ++idx;
            }
        }

        // 5. LIMIT clause
        if (tokenStream[idx].type == LexemeType::KeywordLimit)
        {
            ++idx;
            verifyNext(LexemeType::IntegerLiteral, "Limit Count (Integer)");
            plan.limit_count = tokenStream[idx].numeric_value;
            ++idx;
        }

        verifyNext(LexemeType::StreamEnd, "End of Query");
        return plan;
    }

    Relation executeQuery(const ParsedQuery &plan, const Relation &sourceDatabase)
    {
        if (plan.from_table != sourceDatabase.table_name)
        {
            throw std::runtime_error("Data source mismatch. Table not found: " + plan.from_table);
        }

        // Determine target schema
        std::vector<std::string> targetHeaders = plan.projected_columns.empty() ? sourceDatabase.headers : plan.projected_columns;
        std::vector<int> colMappings;
        colMappings.reserve(targetHeaders.size());

        for (const auto &col : targetHeaders)
        {
            int cIdx = sourceDatabase.getColumnIndex(col);
            if (cIdx < 0)
                throw std::runtime_error("Invalid column requested: " + col);
            colMappings.push_back(cIdx);
        }

        Relation resultSet;
        resultSet.table_name = sourceDatabase.table_name;
        resultSet.headers = targetHeaders;

        // Execute filtering & projection
        for (const auto &row : sourceDatabase.records)
        {
            if (!plan.where_rpn_stack.empty() && !evaluateRPN(plan.where_rpn_stack, sourceDatabase, row))
            {
                continue; // Dropped by WHERE clause
            }

            Tuple projectedRow;
            projectedRow.values.reserve(colMappings.size());
            for (int mIdx : colMappings)
            {
                projectedRow.values.push_back(row.values[static_cast<std::size_t>(mIdx)]);
            }
            resultSet.records.push_back(std::move(projectedRow));
        }

        // Execute Sorting
        if (!plan.order_column.empty())
        {
            int sortIdx = resultSet.getColumnIndex(plan.order_column);
            if (sortIdx < 0)
                throw std::runtime_error("ORDER BY column not found in result output: " + plan.order_column);

            std::stable_sort(resultSet.records.begin(), resultSet.records.end(),
                             [&](const Tuple &r1, const Tuple &r2)
                             {
                                 bool typeValid = true;
                                 int cmp = compareCells(r1.values[static_cast<std::size_t>(sortIdx)],
                                                        r2.values[static_cast<std::size_t>(sortIdx)],
                                                        typeValid);
                                 return plan.sort_descending ? (cmp > 0) : (cmp < 0);
                             });
        }

        // Execute Limitation
        if (plan.limit_count >= 0 && static_cast<long long>(resultSet.records.size()) > plan.limit_count)
        {
            resultSet.records.resize(static_cast<std::size_t>(plan.limit_count));
        }

        return resultSet;
    }

    void printTable(const Relation &table, std::ostream &outputStream)
    {
        const std::size_t numCols = table.headers.size();
        std::vector<std::size_t> maxW(numCols);

        for (std::size_t i = 0; i < numCols; ++i)
        {
            maxW[i] = table.headers[i].size();
        }

        auto stringifyCell = [](const CellValue &cv) -> std::string
        {
            if (std::holds_alternative<long long>(cv))
                return std::to_string(std::get<long long>(cv));
            return std::get<std::string>(cv);
        };

        for (const auto &row : table.records)
        {
            for (std::size_t i = 0; i < numCols; ++i)
            {
                maxW[i] = std::max(maxW[i], stringifyCell(row.values[i]).size());
            }
        }

        auto padString = [&](const std::string &s, std::size_t w)
        {
            outputStream << s << std::string(w - s.size(), ' ');
        };

        // Print Headers
        for (std::size_t i = 0; i < numCols; ++i)
        {
            padString(table.headers[i], maxW[i]);
            outputStream << (i + 1 < numCols ? " | " : "\n");
        }

        // Print Separators
        for (std::size_t i = 0; i < numCols; ++i)
        {
            outputStream << std::string(maxW[i], '-');
            outputStream << (i + 1 < numCols ? "-+-" : "\n");
        }

        // Print Data
        for (const auto &row : table.records)
        {
            for (std::size_t i = 0; i < numCols; ++i)
            {
                padString(stringifyCell(row.values[i]), maxW[i]);
                outputStream << (i + 1 < numCols ? " | " : "\n");
            }
        }

        outputStream << "[" << table.records.size() << " records returned]\n";
    }

} // namespace mini_sql