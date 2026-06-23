#include "catalog/record.h"

#include <cstring>

#include "common/exception.h"

namespace minidb {

std::string Record::serialize(const Schema& schema, const std::vector<Value>& values) {
    if (values.size() != schema.num_columns())
        throw DBException("Record: value count does not match schema");

    std::string out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        const Column& col = schema.column(static_cast<int>(i));
        const Value&  v   = values[i];
        switch (col.type) {
            case ValueType::INT: {
                if (!std::holds_alternative<std::int64_t>(v))
                    throw DBException("Record: expected INT for column " + col.name);
                std::int64_t x = std::get<std::int64_t>(v);
                out.append(reinterpret_cast<const char*>(&x), sizeof(x));
                break;
            }
            case ValueType::DOUBLE: {
                if (!std::holds_alternative<double>(v))
                    throw DBException("Record: expected DOUBLE for column " + col.name);
                double x = std::get<double>(v);
                out.append(reinterpret_cast<const char*>(&x), sizeof(x));
                break;
            }
            case ValueType::VARCHAR: {
                if (!std::holds_alternative<std::string>(v))
                    throw DBException("Record: expected VARCHAR for column " + col.name);
                const std::string& s = std::get<std::string>(v);
                if (s.size() > col.length)
                    throw DBException("Record: VARCHAR too long for column " + col.name);
                std::uint16_t n = static_cast<std::uint16_t>(s.size());
                out.append(reinterpret_cast<const char*>(&n), sizeof(n));
                out.append(s);
                break;
            }
        }
    }
    return out;
}

std::vector<Value> Record::deserialize(const Schema& schema, const char* buf, std::size_t len) {
    std::vector<Value> out;
    out.reserve(schema.num_columns());
    std::size_t pos = 0;

    auto need = [&](std::size_t n) {
        if (pos + n > len) throw DBException("Record: truncated record while decoding");
    };

    for (std::size_t i = 0; i < schema.num_columns(); ++i) {
        const Column& col = schema.column(static_cast<int>(i));
        switch (col.type) {
            case ValueType::INT: {
                need(sizeof(std::int64_t));
                std::int64_t x;
                std::memcpy(&x, buf + pos, sizeof(x));
                pos += sizeof(x);
                out.emplace_back(x);
                break;
            }
            case ValueType::DOUBLE: {
                need(sizeof(double));
                double x;
                std::memcpy(&x, buf + pos, sizeof(x));
                pos += sizeof(x);
                out.emplace_back(x);
                break;
            }
            case ValueType::VARCHAR: {
                need(sizeof(std::uint16_t));
                std::uint16_t n;
                std::memcpy(&n, buf + pos, sizeof(n));
                pos += sizeof(n);
                need(n);
                out.emplace_back(std::string(buf + pos, n));
                pos += n;
                break;
            }
        }
    }
    return out;
}

} // namespace minidb
