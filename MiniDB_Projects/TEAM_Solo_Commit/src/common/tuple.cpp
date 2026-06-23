#include "tuple.h"

#include <cstring>

namespace minidb {

namespace {
void PutU16(std::string& out, uint16_t v) {
    out.push_back(static_cast<char>(v & 0xFF));
    out.push_back(static_cast<char>((v >> 8) & 0xFF));
}
void PutI32(std::string& out, int32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
}
void PutI64(std::string& out, int64_t v) {
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
}
uint16_t GetU16(const std::string& b, size_t& p) {
    uint16_t v = static_cast<uint8_t>(b[p]) | (static_cast<uint8_t>(b[p + 1]) << 8);
    p += 2;
    return v;
}
int32_t GetI32(const std::string& b, size_t& p) {
    int32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= (static_cast<int32_t>(static_cast<uint8_t>(b[p + i])) << (8 * i));
    p += 4;
    return v;
}
int64_t GetI64(const std::string& b, size_t& p) {
    int64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= (static_cast<int64_t>(static_cast<uint8_t>(b[p + i])) << (8 * i));
    p += 8;
    return v;
}
}  // namespace

std::string Tuple::Serialize(const Schema& schema) const {
    std::string out;
    for (size_t i = 0; i < schema.Count(); ++i) {
        const Value& v = values_[i];
        switch (schema.GetColumn(i).type) {
            case TypeId::INTEGER: PutI32(out, v.AsInt()); break;
            case TypeId::BIGINT:  PutI64(out, v.AsBigInt()); break;
            case TypeId::BOOLEAN: out.push_back(v.AsBool() ? 1 : 0); break;
            case TypeId::VARCHAR: {
                const std::string& s = v.AsString();
                PutU16(out, static_cast<uint16_t>(s.size()));
                out += s;
                break;
            }
            default: break;
        }
    }
    return out;
}

Tuple Tuple::Deserialize(const std::string& bytes, const Schema& schema) {
    std::vector<Value> vals;
    vals.reserve(schema.Count());
    size_t p = 0;
    for (size_t i = 0; i < schema.Count(); ++i) {
        switch (schema.GetColumn(i).type) {
            case TypeId::INTEGER: vals.push_back(Value::MakeInt(GetI32(bytes, p))); break;
            case TypeId::BIGINT:  vals.push_back(Value::MakeBigInt(GetI64(bytes, p))); break;
            case TypeId::BOOLEAN: vals.push_back(Value::MakeBool(bytes[p++] != 0)); break;
            case TypeId::VARCHAR: {
                uint16_t len = GetU16(bytes, p);
                vals.push_back(Value::MakeVarchar(bytes.substr(p, len)));
                p += len;
                break;
            }
            default: vals.push_back(Value()); break;
        }
    }
    return Tuple(std::move(vals));
}

std::string Tuple::ToString() const {
    std::string s = "(";
    for (size_t i = 0; i < values_.size(); ++i) {
        if (i) s += ", ";
        s += values_[i].ToString();
    }
    s += ")";
    return s;
}

}  // namespace minidb
