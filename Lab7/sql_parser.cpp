#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>
#include <variant>
#include <sstream>
#include <algorithm>

using Value = std::variant<double,std::string>;

struct Row
{
    std::unordered_map<std::string,Value> cols;
};

struct SelectQuery
{
    std::vector<std::string> columns;
    std::string from;
    std::string where_raw;
    std::string order_by;
    bool order_asc = true;
    int limit = -1;
};

std::vector<std::string> tokenize(
    const std::string& expr);

std::vector<std::string> to_rpn(
    const std::vector<std::string>& tokens);

double eval_rpn(
    const std::vector<std::string>& rpn,
    const std::unordered_map<std::string,double>& vars);

double row_val(
    const Row& row,
    const std::string& col)
{
    auto it = row.cols.find(col);

    if(it==row.cols.end())
        return 0.0;

    if(auto* d =
        std::get_if<double>(&it->second))
        return *d;

    if(auto* s =
        std::get_if<std::string>(&it->second))
    {
        try
        {
            return std::stod(*s);
        }
        catch(...)
        {
        }
    }

    return 0.0;
}

std::string to_upper(std::string s)
{
    for(auto& c : s)
        c = std::toupper(c);

    return s;
}

SelectQuery parse_select(
    const std::string& sql)
{
    SelectQuery q;

    std::istringstream ss(sql);

    std::string word;

    ss >> word;

    while(ss >> word &&
          to_upper(word)!="FROM")
    {
        if(!word.empty() &&
           word.back()==',')
            word.pop_back();

        if(word=="*")
            q.columns.clear();
        else
            q.columns.push_back(word);
    }

    ss >> q.from;

    while(ss >> word)
    {
        std::string kw =
            to_upper(word);

        if(kw=="WHERE")
        {
            std::string clause;
            std::string w2;

            while(ss >> w2)
            {
                if(to_upper(w2)=="ORDER" ||
                   to_upper(w2)=="LIMIT")
                {
                    word = w2;
                    goto next_clause;
                }

                if(!clause.empty())
                    clause += " ";

                clause += w2;
            }

            q.where_raw = clause;
            break;

next_clause:
            q.where_raw = clause;
            kw = to_upper(word);
        }

        if(kw=="ORDER")
        {
            ss >> word;
            ss >> q.order_by;

            std::string dir;

            if(ss >> dir)
            {
                if(to_upper(dir)=="DESC")
                    q.order_asc = false;
            }
        }

        if(kw=="LIMIT")
        {
            ss >> q.limit;
        }
    }

    return q;
}

std::vector<Row> execute(
    const SelectQuery& q,
    const std::vector<Row>& data)
{
    std::vector<std::string> rpn;

    if(!q.where_raw.empty())
        rpn = to_rpn(
            tokenize(q.where_raw));

    std::vector<Row> result;

    for(const auto& row : data)
    {
        if(!rpn.empty())
        {
            std::unordered_map<
                std::string,double> vars;

            for(const auto& [k,v]
                : row.cols)
            {
                vars[k] =
                    row_val(row,k);
            }

            if(!eval_rpn(rpn,vars))
                continue;
        }

        if(q.columns.empty())
        {
            result.push_back(row);
        }
        else
        {
            Row projected;

            for(const auto& col :
                q.columns)
            {
                if(row.cols.count(col))
                    projected.cols[col]
                    = row.cols.at(col);
            }

            result.push_back(projected);
        }
    }

    if(!q.order_by.empty())
    {
        std::sort(
            result.begin(),
            result.end(),
            [&](const Row& a,
                const Row& b)
            {
                double va =
                    row_val(a,q.order_by);

                double vb =
                    row_val(b,q.order_by);

                return q.order_asc
                    ? va < vb
                    : va > vb;
            });
    }

    if(q.limit>=0 &&
       result.size() >
       static_cast<size_t>(q.limit))
    {
        result.resize(q.limit);
    }

    return result;
}

void print_rows(
    const std::vector<Row>& rows)
{
    for(const auto& row : rows)
    {
        for(const auto& [k,v]
            : row.cols)
        {
            std::cout<<k<<"=";

            if(auto* d =
                std::get_if<double>(&v))
                std::cout<<*d;

            if(auto* s =
                std::get_if<std::string>(&v))
                std::cout<<*s;

            std::cout<<" ";
        }

        std::cout<<"\n";
    }
}
