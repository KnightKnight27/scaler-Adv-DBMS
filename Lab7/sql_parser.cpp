#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

std::vector<std::string> tokenize(const std::string& expr);
std::vector<std::string> to_rpn(const std::vector<std::string>& tokens);
double eval_rpn(const std::vector<std::string>& rpn,const std::unordered_map<std::string, double>& vars);

struct Value {
  enum Kind { Double, String };

  Kind kind;
  double d;
  std::string s;

  Value() : kind(Double), d(0.0) {}
  Value(double v) : kind(Double), d(v) {}
  Value(const std::string& v) : kind(String), s(v) {}
  Value(const char* v) : kind(String), s(v) {}
};

struct Row {
  std::unordered_map<std::string, Value> cols;
};

double row_val(const Row& row, const std::string& col) {
  auto it = row.cols.find(col);

  if (it == row.cols.end())
    return 0.0;

  if (it->second.kind == Value::Double)
    return it->second.d;

  try {
    return std::stod(it->second.s);
  } catch (...) {
    return 0.0;
  }
}

struct SelectQuery {
  std::vector<std::string> columns;
  std::string from;
  std::string where_raw;
  std::string order_by;
  bool order_asc;
  int limit;

  SelectQuery() : order_asc(true), limit(-1) {}
};

std::string to_upper_sql(std::string s) {
  for (char& c : s)
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

  return s;
}

SelectQuery parse_select(const std::string& sql) {
  SelectQuery q;

  std::istringstream ss(sql);
  std::string word;

  ss >> word;

  while (ss >> word && to_upper_sql(word) != "FROM") {
    if (!word.empty() && word.back() == ',')
      word.pop_back();

    if (word == "*")
      q.columns.clear();
    else
      q.columns.push_back(word);
  }

  ss >> q.from;

  while (ss >> word) {
    std::string kw = to_upper_sql(word);

    if (kw == "WHERE") {
      std::string clause;
      std::string w2;

      while (ss >> w2) {
        std::string up = to_upper_sql(w2);

        if (up == "ORDER" || up == "LIMIT") {
          word = w2;
          break;
        }

        if (!clause.empty())
          clause += " ";

        clause += w2;
      }

      q.where_raw = clause;

      if (to_upper_sql(word) == "ORDER" || to_upper_sql(word) == "LIMIT") {
        kw = to_upper_sql(word);
      } else {
        break;
      }
    }

    if (kw == "ORDER") {
      ss >> word;  // BY
      ss >> q.order_by;

      std::string dir;

      if (ss >> dir) {
        if (to_upper_sql(dir) == "DESC")
          q.order_asc = false;
      }
    }

    if (kw == "LIMIT") {
      ss >> q.limit;
    }
  }

  return q;
}

std::vector<Row> execute(const SelectQuery& q, const std::vector<Row>& data) {
  std::vector<std::string> rpn;

  if (!q.where_raw.empty())
    rpn = to_rpn(tokenize(q.where_raw));

  std::vector<Row> result;

  for (const auto& row : data) {
    if (!rpn.empty()) {
      std::unordered_map<std::string, double> vars;

      for (const auto& [k, v] : row.cols)
        vars[k] = row_val(row, k);

      if (!eval_rpn(rpn, vars))
        continue;
    }

    if (q.columns.empty()) {
      result.push_back(row);
    } else {
      Row projected;

      for (const auto& col : q.columns) {
        auto it = row.cols.find(col);

        if (it != row.cols.end())
          projected.cols[col] = it->second;
      }

      result.push_back(projected);
    }
  }

  if (!q.order_by.empty()) {
    std::sort(result.begin(), result.end(), [&](const Row& a, const Row& b) {
      double va = row_val(a, q.order_by);
      double vb = row_val(b, q.order_by);

      return q.order_asc ? va < vb : va > vb;
    });
  }

  if (q.limit >= 0 && static_cast<int>(result.size()) > q.limit) {
    result.resize(static_cast<size_t>(q.limit));
  }

  return result;
}

void print_rows(const std::vector<Row>& rows) {
  for (const auto& row : rows) {
    for (const auto& [k, v] : row.cols) {
      std::cout << k << "=";

      if (v.kind == Value::Double)
        std::cout << v.d;
      else
        std::cout << v.s;

      std::cout << " ";
    }

    std::cout << "\n";
  }
}

Row make_row(double id, const std::string& name, double age, double gpa) {
  Row row;

  row.cols["id"] = Value(id);
  row.cols["name"] = Value(name);
  row.cols["age"] = Value(age);
  row.cols["gpa"] = Value(gpa);

  return row;
}