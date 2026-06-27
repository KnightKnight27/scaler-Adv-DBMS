#include "minidb/common/types.h"

#include <algorithm>
#include <cctype>

namespace minidb {

std::string Rid::ToString() const {
  return std::to_string(page_id) + ":" + std::to_string(slot_id);
}

std::string Trim(std::string_view input) {
  std::size_t begin = 0;
  while (begin < input.size() && std::isspace(static_cast<unsigned char>(input[begin]))) {
    ++begin;
  }
  std::size_t end = input.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(input[end - 1]))) {
    --end;
  }
  return std::string(input.substr(begin, end - begin));
}

std::string ToUpper(std::string_view input) {
  std::string out(input);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
    return static_cast<char>(std::toupper(ch));
  });
  return out;
}

std::vector<std::string> SplitCsv(std::string_view input) {
  std::vector<std::string> parts;
  std::string current;
  bool in_quote = false;
  for (char ch : input) {
    if (ch == '\'') {
      in_quote = !in_quote;
      current.push_back(ch);
    } else if (ch == ',' && !in_quote) {
      parts.push_back(Trim(current));
      current.clear();
    } else {
      current.push_back(ch);
    }
  }
  if (!current.empty() || !parts.empty()) {
    parts.push_back(Trim(current));
  }
  return parts;
}

std::string Escape(Value value) {
  std::string out;
  out.reserve(value.size());
  for (char ch : value) {
    if (ch == '\\' || ch == '|') {
      out.push_back('\\');
    }
    out.push_back(ch);
  }
  return out;
}

Value Unescape(std::string_view value) {
  std::string out;
  bool escaped = false;
  for (char ch : value) {
    if (escaped) {
      out.push_back(ch);
      escaped = false;
    } else if (ch == '\\') {
      escaped = true;
    } else {
      out.push_back(ch);
    }
  }
  return out;
}

std::string EncodeRow(const Row& row) {
  std::string out;
  for (std::size_t i = 0; i < row.size(); ++i) {
    if (i != 0) {
      out.push_back('|');
    }
    out += Escape(row[i]);
  }
  return out;
}

Row DecodeRow(std::string_view encoded) {
  Row row;
  std::string current;
  bool escaped = false;
  for (char ch : encoded) {
    if (escaped) {
      current.push_back(ch);
      escaped = false;
    } else if (ch == '\\') {
      escaped = true;
    } else if (ch == '|') {
      row.push_back(current);
      current.clear();
    } else {
      current.push_back(ch);
    }
  }
  row.push_back(current);
  return row;
}

}  // namespace minidb
