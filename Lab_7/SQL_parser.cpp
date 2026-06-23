#include <algorithm>
#include <functional>
#include "shunting_yard.cpp"

struct DataValue {
    enum TypeKind { Double, String } kind;
    double floatVal;
    std::string stringVal;
    DataValue() : kind(Double), floatVal(0.0) {}
    DataValue(double val) : kind(Double), floatVal(val) {}
    DataValue(const std::string& val) : kind(String), stringVal(val) {}
    DataValue(const char* val) : kind(String), stringVal(val) {}
};

struct Record
{
    std::unordered_map<std::string, DataValue> fields;
};

double getRecordVal(const Record &rec, const std::string &columnName)
{
    auto it = rec.fields.find(columnName);
    if (it == rec.fields.end())
        return 0.0;
    if (it->second.kind == DataValue::Double)
        return it->second.floatVal;
    if (it->second.kind == DataValue::String)
    {
        try {
            return std::stod(it->second.stringVal);
        }
        catch (...) {}
    }
    return 0.0;
}

struct ParsedSelect
{
    std::vector<std::string> selectCols;
    std::string tableName;
    std::string whereExpression;
    std::string orderByCol;
    bool isAscending;
    int maxRows;

    ParsedSelect() : isAscending(true), maxRows(-1) {}
};

std::string uppercaseString(std::string str)
{
    for (size_t i = 0; i < str.size(); ++i)
        str[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(str[i])));
    return str;
}

ParsedSelect parseSelectSQL(const std::string &sqlQuery)
{
    ParsedSelect parsedQuery;
    std::istringstream stream(sqlQuery);
    std::string token;
    stream >> token; // read SELECT keyword

    while (stream >> token && uppercaseString(token) != "FROM")
    {
        if (!token.empty() && token.back() == ',')
            token.pop_back();
        if (token == "*")
            parsedQuery.selectCols.clear();
        else
            parsedQuery.selectCols.push_back(token);
    }

    stream >> parsedQuery.tableName;

    while (stream >> token)
    {
        std::string upperToken = uppercaseString(token);
        if (upperToken == "WHERE")
        {
            std::string whereClause, tempToken;
            while (stream >> tempToken)
            {
                if (uppercaseString(tempToken) == "ORDER" || uppercaseString(tempToken) == "LIMIT")
                {
                    token = tempToken;
                    goto handle_clause;
                }
                whereClause += (whereClause.empty() ? "" : " ") + tempToken;
            }
            parsedQuery.whereExpression = whereClause;
            break;
        handle_clause:
            parsedQuery.whereExpression = whereClause;
            upperToken = uppercaseString(token);
        }
        if (upperToken == "ORDER")
        {
            stream >> token; // read BY keyword
            stream >> parsedQuery.orderByCol;
            std::string sortDirection;
            if (stream >> sortDirection && uppercaseString(sortDirection) == "DESC")
                parsedQuery.isAscending = false;
        }
        if (upperToken == "LIMIT")
        {
            stream >> parsedQuery.maxRows;
        }
    }
    return parsedQuery;
}

std::vector<Record> runQueryPlan(const ParsedSelect &plan, const std::vector<Record> &dataset)
{
    std::vector<std::string> postfixExpr;
    if (!plan.whereExpression.empty())
        postfixExpr = shuntingYardRPN(tokenizeExpr(plan.whereExpression));

    std::vector<Record> finalResult;

    for (size_t index = 0; index < dataset.size(); ++index)
    {
        const Record &rec = dataset[index];
        if (!postfixExpr.empty())
        {
            std::unordered_map<std::string, double> varBindings;
            for (auto it = rec.fields.begin(); it != rec.fields.end(); ++it)
                varBindings[it->first] = getRecordVal(rec, it->first);
            if (!evaluateRPN(postfixExpr, varBindings))
                continue;
        }

        if (plan.selectCols.empty())
        {
            finalResult.push_back(rec);
        }
        else
        {
            Record projection;
            for (size_t colIdx = 0; colIdx < plan.selectCols.size(); ++colIdx)
            {
                const std::string &column = plan.selectCols[colIdx];
                auto it = rec.fields.find(column);
                if (it != rec.fields.end())
                    projection.fields[column] = it->second;
            }
            finalResult.push_back(projection);
        }
    }

    if (!plan.orderByCol.empty())
    {
        std::sort(finalResult.begin(), finalResult.end(), [&](const Record &left, const Record &right)
        {
            double leftVal = getRecordVal(left, plan.orderByCol);
            double rightVal = getRecordVal(right, plan.orderByCol);
            return plan.isAscending ? leftVal < rightVal : leftVal > rightVal;
        });
    }

    if (plan.maxRows >= 0 && static_cast<int>(finalResult.size()) > plan.maxRows)
        finalResult.resize(static_cast<size_t>(plan.maxRows));

    return finalResult;
}

void displayRecords(const std::vector<Record> &records)
{
    for (size_t index = 0; index < records.size(); ++index)
    {
        const Record &rec = records[index];
        for (auto it = rec.fields.begin(); it != rec.fields.end(); ++it)
        {
            std::cout << it->first << "=";
            if (it->second.kind == DataValue::Double)
                std::cout << it->second.floatVal;
            else
                std::cout << it->second.stringVal;
            std::cout << "  ";
        }
        std::cout << "\n";
    }
}

Record createRecord(double id, const std::string &name, double age, double gpa)
{
    Record rec;
    rec.fields["id"] = DataValue(id);
    rec.fields["name"] = DataValue(name);
    rec.fields["age"] = DataValue(age);
    rec.fields["gpa"] = DataValue(gpa);
    return rec;
}