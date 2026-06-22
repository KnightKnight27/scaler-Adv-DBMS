#pragma once

#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../index/btree.h"
#include "../types.h"

namespace minidb {

// Everything the engine knows about one table: its schema, the pages holding
// its tuples, an optional primary index on the first integer column, and a
// cached row count used by the optimizer.
struct TableInfo {
    std::string name;
    Schema schema;
    std::vector<int> page_ids;
    int index_col = -1;
    std::unique_ptr<BPlusTree> index;
    int64_t num_tuples = 0;
};

class Catalog {
public:
    bool exists(const std::string& n) const { return tables_.count(n) > 0; }
    TableInfo& get(const std::string& n) { return *tables_.at(n); }
    const std::map<std::string, std::unique_ptr<TableInfo>>& all() const { return tables_; }

    TableInfo& create(const std::string& name, const Schema& schema) {
        auto ti = std::make_unique<TableInfo>();
        ti->name = name;
        ti->schema = schema;
        for (size_t i = 0; i < schema.size(); ++i)
            if (schema[i].type == Type::Int) {
                ti->index_col = static_cast<int>(i);
                break;
            }
        if (ti->index_col >= 0) ti->index = std::make_unique<BPlusTree>();
        TableInfo& ref = *ti;
        tables_[name] = std::move(ti);
        return ref;
    }

    void save(const std::string& path) const {
        std::ofstream o(path, std::ios::binary | std::ios::trunc);
        wr<int32_t>(o, static_cast<int32_t>(tables_.size()));
        for (const auto& kv : tables_) {
            const TableInfo& ti = *kv.second;
            wstr(o, ti.name);
            wr<int32_t>(o, static_cast<int32_t>(ti.schema.size()));
            for (const auto& c : ti.schema) {
                wstr(o, c.name);
                wr<int32_t>(o, static_cast<int32_t>(c.type));
            }
            wr<int32_t>(o, static_cast<int32_t>(ti.page_ids.size()));
            for (int p : ti.page_ids) wr<int32_t>(o, p);
            wr<int32_t>(o, static_cast<int32_t>(ti.index_col));
            wr<int64_t>(o, ti.num_tuples);
        }
    }

    void load(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) return;
        int32_t nt;
        if (!rd(in, nt)) return;
        for (int32_t i = 0; i < nt; ++i) {
            auto ti = std::make_unique<TableInfo>();
            ti->name = rstr(in);
            int32_t nc;
            rd(in, nc);
            for (int32_t c = 0; c < nc; ++c) {
                std::string cn = rstr(in);
                int32_t ct;
                rd(in, ct);
                ti->schema.push_back({cn, static_cast<Type>(ct)});
            }
            int32_t np;
            rd(in, np);
            for (int32_t p = 0; p < np; ++p) {
                int32_t pid;
                rd(in, pid);
                ti->page_ids.push_back(pid);
            }
            rd(in, ti->index_col);
            rd(in, ti->num_tuples);
            if (ti->index_col >= 0) ti->index = std::make_unique<BPlusTree>();
            tables_[ti->name] = std::move(ti);
        }
    }

private:
    std::map<std::string, std::unique_ptr<TableInfo>> tables_;

    template <class T>
    static void wr(std::ostream& o, T v) {
        o.write(reinterpret_cast<const char*>(&v), sizeof(T));
    }
    template <class T>
    static bool rd(std::istream& i, T& v) {
        return static_cast<bool>(i.read(reinterpret_cast<char*>(&v), sizeof(T)));
    }
    static void wstr(std::ostream& o, const std::string& s) {
        wr<int32_t>(o, static_cast<int32_t>(s.size()));
        o.write(s.data(), s.size());
    }
    static std::string rstr(std::istream& i) {
        int32_t n;
        rd(i, n);
        std::string s;
        s.resize(n);
        if (n) i.read(&s[0], n);
        return s;
    }
};

}  // namespace minidb
