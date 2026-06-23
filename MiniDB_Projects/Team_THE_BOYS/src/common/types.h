#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace minidb {

constexpr std::size_t PAGE_SIZE = 4096;
constexpr int INVALID_PAGE_ID = -1;
constexpr std::size_t DEFAULT_BUFFER_POOL_SIZE = 64;
constexpr std::size_t BATCH_SIZE = 256;

enum class ValueType { INT, STRING, NULL_TYPE };

using ValueData = std::variant<std::monostate, int64_t, std::string>;

struct Value {
    ValueType type = ValueType::NULL_TYPE;
    ValueData data;

    static Value Null();
    static Value Int(int64_t v);
    static Value Str(std::string v);

    bool operator==(const Value& other) const;
    bool operator!=(const Value& other) const { return !(*this == other); }
    bool operator<(const Value& other) const;

    std::string ToString() const;
};

struct ColumnDef {
    std::string name;
    ValueType type = ValueType::INT;
    bool primary_key = false;
    bool indexed = false;
};

struct Rid {
    int page_id = INVALID_PAGE_ID;
    int slot_id = -1;

    bool valid() const { return page_id >= 0 && slot_id >= 0; }
    bool operator==(const Rid& other) const {
        return page_id == other.page_id && slot_id == other.slot_id;
    }
};

struct Row {
    std::vector<Value> values;
    std::optional<Rid> rid;

    const Value& Get(std::size_t idx) const { return values.at(idx); }
};

enum class LockMode { SHARED, EXCLUSIVE };

enum class LogRecordType {
    BEGIN,
    COMMIT,
    ABORT,
    INSERT,
    DELETE_TUP,
    CHECKPOINT
};

enum class CompareOp { EQ, NE, LT, LE, GT, GE };

struct Predicate {
    std::string column;
    CompareOp op = CompareOp::EQ;
    Value value;
};

struct JoinSpec {
    std::string left_table;
    std::string right_table;
    std::string left_col;
    std::string right_col;
};

bool CompareValues(const Value& lhs, CompareOp op, const Value& rhs);
CompareOp ParseCompareOp(const std::string& token);

}  // namespace minidb
