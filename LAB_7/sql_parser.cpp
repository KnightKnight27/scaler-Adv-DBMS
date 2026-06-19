#include <algorithm>
#include <functional>
#include "shunting_yard.cpp"

struct Value {
    enum Kind { Double, String } kind;
    double d;
    std::string s;
    Value() : kind(Double), d(0.0) {}
    Value(double v) : kind(Double), d(v) {}
    Value(const std::string& v) : kind(String), s(v) {}
    Value(const char* v) : kind(String), s(v) {}
};

struct Row
{
    std::unordered_map<std::string, Value> cols;
};

double row_val(const Row &row, const std::string &col)
{
    auto it = row.cols.find(col);
    if (it == row.cols.end())
        return 0.0;
    if (it->second.kind == Value::Double)
        return it->second.d;
    if (it->second.kind == Value::String)
    {
        try{ return std::stod(it->second.s); }
        catch (...){ }
    }
    return 0.0;
}

struct SelectQuery
{
    std::vector<std::string> columns;
    std::string from;
    std::string where_raw;
    std::string order_by;
    bool order_asc;
    int limit;

    SelectQuery() : order_asc(true), limit(-1) {}
};

std::string to_upper(std::string s)
{
    for (size_t i = 0; i < s.size(); ++i)
        s[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[i])));
    return s;
}

SelectQuery parse_select(const std::string &sql)
{
    SelectQuery q;
    std::istringstream ss(sql);
    std::string word;
    ss >> word;

    while (ss >> word && to_upper(word) != "FROM")
    {
        if (!word.empty() && word.back() == ',')
            word.pop_back();
        if (word == "*")
            q.columns.clear();
        else
            q.columns.push_back(word);
    }

    ss >> q.from;

    while (ss >> word)
    {
        std::string kw = to_upper(word);
        if (kw == "WHERE")
        {
            std::string clause, w2;
            while (ss >> w2)
            {
                if (to_upper(w2) == "ORDER" || to_upper(w2) == "LIMIT")
                {
                    word = w2;
                    goto next_clause;
                }
                clause += (clause.empty() ? "" : " ") + w2;
            }
            q.where_raw = clause;
            break;
        next_clause:
            q.where_raw = clause;
            kw = to_upper(word);
        }
        if (kw == "ORDER")
        {
            ss >> word;
            ss >> q.order_by;
            std::string dir;
            if (ss >> dir && to_upper(dir) == "DESC")
                q.order_asc = false;
        }
        if (kw == "LIMIT")
        {
            ss >> q.limit;
        }
    }
    return q;
}

std::vector<Row> execute(const SelectQuery &q, const std::vector<Row> &data)
{
    std::vector<std::string> rpn;
    if (!q.where_raw.empty())
        rpn = to_rpn(tokenize(q.where_raw));

    std::vector<Row> result;

    for (size_t ri = 0; ri < data.size(); ++ri)
    {
        const Row &row = data[ri];
        if (!rpn.empty())
        {
            std::unordered_map<std::string, double> vars;
            for (auto it = row.cols.begin(); it != row.cols.end(); ++it)
                vars[it->first] = row_val(row, it->first);
            if (!eval_rpn(rpn, vars))
                continue;
        }

        if (q.columns.empty())
        {
            result.push_back(row);
        }
        else
        {
            Row projected;
            for (size_t ci = 0; ci < q.columns.size(); ++ci)
            {
                const std::string &col = q.columns[ci];
                auto it = row.cols.find(col);
                if (it != row.cols.end())
                    projected.cols[col] = it->second;
            }
            result.push_back(projected);
        }
    }

    if (!q.order_by.empty())
    {
        std::sort(result.begin(), result.end(), [&](const Row &a, const Row &b)
                  {
            double va = row_val(a, q.order_by);
            double vb = row_val(b, q.order_by);
            return q.order_asc ? va < vb : va > vb; });
    }

    if (q.limit >= 0 && static_cast<int>(result.size()) > q.limit)
        result.resize(static_cast<size_t>(q.limit));

    return result;
}

void print_rows(const std::vector<Row> &rows)
{
    for (size_t ri = 0; ri < rows.size(); ++ri)
    {
        const Row &row = rows[ri];
        for (auto it = row.cols.begin(); it != row.cols.end(); ++it)
        {
            std::cout << it->first << "=";
            if (it->second.kind == Value::Double)
                std::cout << it->second.d;
            else
                std::cout << it->second.s;
            std::cout << "  ";
        }
        std::cout << "\n";
    }
}

Row make_row(double id, const std::string &name, double age, double gpa)
{
    Row row;
    row.cols["id"] = Value(id);
    row.cols["name"] = Value(name);
    row.cols["age"] = Value(age);
    row.cols["gpa"] = Value(gpa);
    return row;
}