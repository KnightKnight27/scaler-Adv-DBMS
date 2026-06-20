#include "shunting_yard.cpp"
#include <algorithm>
#include <sstream>

struct VariantVal {
  enum Type { Double, String } dataType;
  double numericVal;
  std::string stringVal;

  VariantVal() : dataType(Double), numericVal(0.0) {}
  VariantVal(double num) : dataType(Double), numericVal(num) {}
  VariantVal(const std::string &str) : dataType(String), stringVal(str) {}
  VariantVal(const char *rawStr) : dataType(String), stringVal(rawStr) {}
};

struct DataRecord {
  std::unordered_map<std::string, VariantVal> fields;
};

double extractDoubleValue(const DataRecord &record,
                          const std::string &fieldName) {
  auto fieldIter = record.fields.find(fieldName);
  if (fieldIter == record.fields.end()) {
    return 0.0;
  }

  const VariantVal &valObj = fieldIter->second;
  if (valObj.dataType == VariantVal::Double) {
    return valObj.numericVal;
  } else if (valObj.dataType == VariantVal::String) {
    try {
      return std::stod(valObj.stringVal);
    } catch (...) {
    }
  }
  return 0.0;
}

struct ParsedQuery {
  std::vector<std::string> targetColumns;
  std::string tableName;
  std::string filterClause;
  std::string sortColumn;
  bool isSortAscending;
  int maxRows;

  ParsedQuery() : isSortAscending(true), maxRows(-1) {}
};

std::string convertStringToUpper(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return std::toupper(c); });
  return text;
}

ParsedQuery parseSelectStatement(const std::string &queryText) {
  ParsedQuery stmt;
  std::istringstream queryStream(queryText);
  std::string token;
  queryStream >> token;

  while (queryStream >> token && convertStringToUpper(token) != "FROM") {
    if (!token.empty() && token.back() == ',') {
      token.pop_back();
    }

    if (token == "*") {
      stmt.targetColumns.clear();
    } else {
      stmt.targetColumns.push_back(token);
    }
  }

  queryStream >> stmt.tableName;

  while (queryStream >> token) {
    std::string keyword = convertStringToUpper(token);

    if (keyword == "WHERE") {
      std::string whereString, subToken;
      bool reachedNextKeyword = false;

      while (queryStream >> subToken) {
        std::string upperSub = convertStringToUpper(subToken);
        if (upperSub == "ORDER" || upperSub == "LIMIT") {
          token = subToken;
          reachedNextKeyword = true;
          break;
        }
        if (!whereString.empty()) {
          whereString += " ";
        }
        whereString += subToken;
      }
      stmt.filterClause = whereString;
      if (!reachedNextKeyword) {
        break;
      }
      keyword = convertStringToUpper(token);
    }

    if (keyword == "ORDER") {
      queryStream >> token;
      queryStream >> stmt.sortColumn;
      std::string direction;
      if (queryStream >> direction &&
          convertStringToUpper(direction) == "DESC") {
        stmt.isSortAscending = false;
      }
    }

    if (keyword == "LIMIT") {
      queryStream >> stmt.maxRows;
    }
  }
  return stmt;
}

std::vector<DataRecord>
executeSelectQuery(const ParsedQuery &query,
                   const std::vector<DataRecord> &records) {
  std::vector<std::string> postfixFilter;
  if (!query.filterClause.empty()) {
    postfixFilter = convertIntoRPN(extractTokens(query.filterClause));
  }

  std::vector<DataRecord> resultSet;

  for (size_t rowIdx = 0; rowIdx < records.size(); ++rowIdx) {
    const DataRecord &record = records[rowIdx];
    if (!postfixFilter.empty()) {
      std::unordered_map<std::string, double> evalEnv;
      for (auto it = record.fields.begin(); it != record.fields.end(); ++it) {
        evalEnv[it->first] = extractDoubleValue(record, it->first);
      }

      if (!evaluateRPN(postfixFilter, evalEnv)) {
        continue;
      }
    }

    if (query.targetColumns.empty()) {
      resultSet.push_back(record);
    } else {
      DataRecord newRecord;
      for (size_t colIdx = 0; colIdx < query.targetColumns.size(); ++colIdx) {
        const std::string &columnName = query.targetColumns[colIdx];
        auto it = record.fields.find(columnName);
        if (it != record.fields.end()) {
          newRecord.fields[columnName] = it->second;
        }
      }
      resultSet.push_back(newRecord);
    }
  }

  if (!query.sortColumn.empty()) {
    std::sort(resultSet.begin(), resultSet.end(),
              [&](const DataRecord &a, const DataRecord &b) {
                double valA = extractDoubleValue(a, query.sortColumn);
                double valB = extractDoubleValue(b, query.sortColumn);
                return query.isSortAscending ? (valA < valB) : (valA > valB);
              });
  }

  if (query.maxRows >= 0 &&
      static_cast<int>(resultSet.size()) > query.maxRows) {
    resultSet.resize(static_cast<size_t>(query.maxRows));
  }

  return resultSet;
}

void outputResultSet(const std::vector<DataRecord> &resultSet) {
  for (size_t rowIdx = 0; rowIdx < resultSet.size(); ++rowIdx) {
    const DataRecord &record = resultSet[rowIdx];
    for (auto it = record.fields.begin(); it != record.fields.end(); ++it) {
      std::cout << it->first << "=";
      if (it->second.dataType == VariantVal::Double) {
        std::cout << it->second.numericVal;
      } else {
        std::cout << it->second.stringVal;
      }
      std::cout << "  ";
    }
    std::cout << "\n";
  }
}

DataRecord createNewRecord(double idVal, const std::string &nameVal,
                           double ageVal, double gpaVal) {
  DataRecord record;
  record.fields["id"] = VariantVal(idVal);
  record.fields["name"] = VariantVal(nameVal);
  record.fields["age"] = VariantVal(ageVal);
  record.fields["gpa"] = VariantVal(gpaVal);
  return record;
}