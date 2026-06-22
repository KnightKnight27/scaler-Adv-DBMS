#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace minidb {

enum class Type { Int, Text };

struct Value {
    Type type = Type::Int;
    int64_t i = 0;
    std::string s;

    static Value make_int(int64_t v) {
        Value x;
        x.type = Type::Int;
        x.i = v;
        return x;
    }
    static Value make_text(std::string v) {
        Value x;
        x.type = Type::Text;
        x.s = std::move(v);
        return x;
    }

    bool operator==(const Value& o) const {
        if (type != o.type) return false;
        return type == Type::Int ? i == o.i : s == o.s;
    }
    bool operator<(const Value& o) const {
        return type == Type::Int ? i < o.i : s < o.s;
    }

    std::string to_string() const {
        return type == Type::Int ? std::to_string(i) : s;
    }
};

struct Column {
    std::string name;
    Type type;
};

using Schema = std::vector<Column>;
using Tuple = std::vector<Value>;

struct RID {
    int32_t page_id = -1;
    int32_t slot_id = -1;
    bool operator==(const RID& o) const {
        return page_id == o.page_id && slot_id == o.slot_id;
    }
};

inline std::vector<uint8_t> serialize_tuple(const Schema& schema, const Tuple& t) {
    std::vector<uint8_t> out;
    auto put_i64 = [&](int64_t v) {
        uint8_t b[8];
        std::memcpy(b, &v, 8);
        out.insert(out.end(), b, b + 8);
    };
    auto put_i32 = [&](int32_t v) {
        uint8_t b[4];
        std::memcpy(b, &v, 4);
        out.insert(out.end(), b, b + 4);
    };
    for (size_t c = 0; c < schema.size(); ++c) {
        if (schema[c].type == Type::Int) {
            put_i64(t[c].i);
        } else {
            put_i32(static_cast<int32_t>(t[c].s.size()));
            out.insert(out.end(), t[c].s.begin(), t[c].s.end());
        }
    }
    return out;
}

inline Tuple deserialize_tuple(const Schema& schema, const uint8_t* data, int len) {
    Tuple t;
    int off = 0;
    auto get_i64 = [&]() {
        int64_t v;
        std::memcpy(&v, data + off, 8);
        off += 8;
        return v;
    };
    auto get_i32 = [&]() {
        int32_t v;
        std::memcpy(&v, data + off, 4);
        off += 4;
        return v;
    };
    for (size_t c = 0; c < schema.size(); ++c) {
        if (schema[c].type == Type::Int) {
            t.push_back(Value::make_int(get_i64()));
        } else {
            int32_t n = get_i32();
            std::string str(reinterpret_cast<const char*>(data + off), n);
            off += n;
            t.push_back(Value::make_text(std::move(str)));
        }
    }
    (void)len;
    return t;
}

inline int column_index(const Schema& schema, const std::string& name) {
    for (size_t i = 0; i < schema.size(); ++i) {
        const std::string& cn = schema[i].name;
        if (cn == name) return static_cast<int>(i);
        // accept an unqualified match against a qualified "table.col"
        auto dot = cn.find('.');
        if (dot != std::string::npos && cn.substr(dot + 1) == name) return static_cast<int>(i);
        auto qdot = name.find('.');
        if (qdot != std::string::npos && name.substr(qdot + 1) == cn) return static_cast<int>(i);
    }
    return -1;
}

}  // namespace minidb
