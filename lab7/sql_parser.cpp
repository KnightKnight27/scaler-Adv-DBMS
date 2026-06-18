#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>
#include <variant>
#include <sstream>
#include <algorithm>

using CellData = std::variant<double, std::string>;

struct Record
{
    std::unordered_map<std::string, CellData> values;
};

struct QueryPlan
{
    std::vector<std::string> selectedFields;
    std::string sourceTable;
    std::string filterExpression;
    std::string sortField;
    bool ascending = true;
    int maxRows = -1;
};

std::vector<std::string> splitExpression(
    const std::string& expression);

std::vector<std::string> buildPostfix(
    const std::vector<std::string>& tokens);

double evaluatePostfix(
    const std::vector<std::string>& postfix,
    const std::unordered_map<std::string,double>& variables);

double extractNumber(
    const Record& record,
    const std::string& field)
{
    auto itr = record.values.find(field);

    if (itr == record.values.end())
        return 0.0;

    if (std::holds_alternative<double>(itr->second))
        return std::get<double>(itr->second);

    try
    {
        return std::stod(
            std::get<std::string>(itr->second));
    }
    catch (...)
    {
        return 0.0;
    }
}

std::string makeUpper(std::string text)
{
    for (char& ch : text)
        ch = std::toupper(ch);

    return text;
}

QueryPlan buildQuery(
    const std::string& sql)
{
    QueryPlan query;

    std::istringstream stream(sql);
    std::string token;

    stream >> token;

    while (stream >> token)
    {
        if (makeUpper(token) == "FROM")
            break;

        if (!token.empty() && token.back() == ',')
            token.pop_back();

        if (token != "*")
            query.selectedFields.push_back(token);
    }

    stream >> query.sourceTable;

    std::vector<std::string> remaining;
    while (stream >> token)
        remaining.push_back(token);

    for (size_t i = 0; i < remaining.size(); ++i)
    {
        std::string keyword =
            makeUpper(remaining[i]);

        if (keyword == "WHERE")
        {
            ++i;

            while (i < remaining.size())
            {
                std::string current =
                    makeUpper(remaining[i]);

                if (current == "ORDER" ||
                    current == "LIMIT")
                {
                    --i;
                    break;
                }

                if (!query.filterExpression.empty())
                    query.filterExpression += " ";

                query.filterExpression +=
                    remaining[i];

                ++i;
            }
        }
        else if (keyword == "ORDER")
        {
            if (i + 2 < remaining.size())
            {
                i++;
                query.sortField =
                    remaining[++i];

                if (i + 1 < remaining.size())
                {
                    std::string direction =
                        makeUpper(remaining[i + 1]);

                    if (direction == "DESC")
                    {
                        query.ascending = false;
                        ++i;
                    }
                }
            }
        }
        else if (keyword == "LIMIT")
        {
            if (i + 1 < remaining.size())
            {
                query.maxRows =
                    std::stoi(remaining[++i]);
            }
        }
    }

    return query;
}

std::vector<Record> runQuery(
    const QueryPlan& query,
    const std::vector<Record>& table)
{
    std::vector<std::string> postfix;

    if (!query.filterExpression.empty())
    {
        postfix =
            buildPostfix(
                splitExpression(
                    query.filterExpression));
    }

    std::vector<Record> output;

    for (const Record& record : table)
    {
        if (!postfix.empty())
        {
            std::unordered_map<
                std::string,double> variables;

            for (const auto& entry :
                 record.values)
            {
                variables[entry.first] =
                    extractNumber(
                        record,
                        entry.first);
            }

            if (!evaluatePostfix(
                    postfix,
                    variables))
            {
                continue;
            }
        }

        if (query.selectedFields.empty())
        {
            output.push_back(record);
            continue;
        }

        Record projected;

        for (const auto& field :
             query.selectedFields)
        {
            auto itr =
                record.values.find(field);

            if (itr != record.values.end())
            {
                projected.values.insert(
                    *itr);
            }
        }

        output.push_back(projected);
    }

    if (!query.sortField.empty())
    {
        std::sort(
            output.begin(),
            output.end(),
            [&](const Record& left,
                const Record& right)
            {
                double a =
                    extractNumber(
                        left,
                        query.sortField);

                double b =
                    extractNumber(
                        right,
                        query.sortField);

                return query.ascending
                    ? a < b
                    : a > b;
            });
    }

    if (query.maxRows >= 0 &&
        output.size() >
        static_cast<size_t>(query.maxRows))
    {
        output.resize(query.maxRows);
    }

    return output;
}

void displayResults(
    const std::vector<Record>& records)
{
    for (const Record& record : records)
    {
        for (const auto& item :
             record.values)
        {
            std::cout
                << item.first
                << ": ";

            std::visit(
                [](const auto& value)
                {
                    std::cout << value;
                },
                item.second);

            std::cout << "  ";
        }

        std::cout << '\n';
    }
}