#include "log_record.h"

namespace minidb {

namespace {
void PutU8(std::string& o, uint8_t v) { o.push_back(static_cast<char>(v)); }
void PutU16(std::string& o, uint16_t v) { o.push_back(v & 0xFF); o.push_back((v >> 8) & 0xFF); }
void PutU32(std::string& o, uint32_t v) { for (int i = 0; i < 4; ++i) o.push_back((v >> (8 * i)) & 0xFF); }
void PutI64(std::string& o, int64_t v) { for (int i = 0; i < 8; ++i) o.push_back((v >> (8 * i)) & 0xFF); }
void PutStr(std::string& o, const std::string& s) { PutU16(o, static_cast<uint16_t>(s.size())); o += s; }

uint8_t GetU8(const std::string& b, size_t& p) { return static_cast<uint8_t>(b[p++]); }
uint16_t GetU16(const std::string& b, size_t& p) { uint16_t v = static_cast<uint8_t>(b[p]) | (static_cast<uint8_t>(b[p+1]) << 8); p += 2; return v; }
uint32_t GetU32(const std::string& b, size_t& p) { uint32_t v = 0; for (int i = 0; i < 4; ++i) v |= (static_cast<uint32_t>(static_cast<uint8_t>(b[p+i])) << (8*i)); p += 4; return v; }
int64_t GetI64(const std::string& b, size_t& p) { int64_t v = 0; for (int i = 0; i < 8; ++i) v |= (static_cast<int64_t>(static_cast<uint8_t>(b[p+i])) << (8*i)); p += 8; return v; }
std::string GetStr(const std::string& b, size_t& p) { uint16_t n = GetU16(b, p); std::string s = b.substr(p, n); p += n; return s; }

// Self-describing value encoding (recovery may parse before the schema is known).
void PutValue(std::string& o, const Value& v) {
    PutU8(o, static_cast<uint8_t>(v.type()));
    switch (v.type()) {
        case TypeId::INTEGER: PutI64(o, v.AsInt()); break;
        case TypeId::BIGINT:  PutI64(o, v.AsBigInt()); break;
        case TypeId::BOOLEAN: PutU8(o, v.AsBool() ? 1 : 0); break;
        case TypeId::VARCHAR: PutStr(o, v.AsString()); break;
        default: break;
    }
}
Value GetValue(const std::string& b, size_t& p) {
    TypeId t = static_cast<TypeId>(GetU8(b, p));
    switch (t) {
        case TypeId::INTEGER: return Value::MakeInt(static_cast<int32_t>(GetI64(b, p)));
        case TypeId::BIGINT:  return Value::MakeBigInt(GetI64(b, p));
        case TypeId::BOOLEAN: return Value::MakeBool(GetU8(b, p) != 0);
        case TypeId::VARCHAR: return Value::MakeVarchar(GetStr(b, p));
        default: return Value();
    }
}
}  // namespace

std::string SerializeRecord(const LogRecord& r) {
    std::string body;
    PutU8(body, static_cast<uint8_t>(r.type));
    PutU32(body, static_cast<uint32_t>(r.txn_id));
    PutStr(body, r.table);
    switch (r.type) {
        case LogType::Insert:
        case LogType::Delete:
            PutU16(body, static_cast<uint16_t>(r.row.size()));
            for (const Value& v : r.row) PutValue(body, v);
            break;
        case LogType::CreateTable:
            PutU16(body, static_cast<uint16_t>(r.columns.size()));
            for (const Column& c : r.columns) {
                PutStr(body, c.name);
                PutU8(body, static_cast<uint8_t>(c.type));
                PutU8(body, c.is_primary_key ? 1 : 0);
            }
            break;
        case LogType::CreateIndex:
            PutStr(body, r.column);
            PutU8(body, r.unique ? 1 : 0);
            break;
        default: break;
    }
    std::string out;
    PutU32(out, static_cast<uint32_t>(body.size()));  // length prefix
    out += body;
    return out;
}

bool DeserializeRecord(const std::string& buf, size_t& pos, LogRecord* out) {
    if (pos + 4 > buf.size()) return false;
    uint32_t len = GetU32(buf, pos);
    if (pos + len > buf.size()) return false;  // truncated tail (torn write): stop here
    size_t end = pos + len;

    out->type = static_cast<LogType>(GetU8(buf, pos));
    out->txn_id = static_cast<int>(GetU32(buf, pos));
    out->table = GetStr(buf, pos);
    out->row.clear();
    out->columns.clear();
    out->column.clear();
    out->unique = false;

    switch (out->type) {
        case LogType::Insert:
        case LogType::Delete: {
            uint16_t n = GetU16(buf, pos);
            for (uint16_t i = 0; i < n; ++i) out->row.push_back(GetValue(buf, pos));
            break;
        }
        case LogType::CreateTable: {
            uint16_t n = GetU16(buf, pos);
            for (uint16_t i = 0; i < n; ++i) {
                Column c;
                c.name = GetStr(buf, pos);
                c.type = static_cast<TypeId>(GetU8(buf, pos));
                c.is_primary_key = GetU8(buf, pos) != 0;
                out->columns.push_back(c);
            }
            break;
        }
        case LogType::CreateIndex:
            out->column = GetStr(buf, pos);
            out->unique = GetU8(buf, pos) != 0;
            break;
        default: break;
    }
    pos = end;  // tolerate any padding
    return true;
}

}  // namespace minidb
