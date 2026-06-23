#include "recovery/log_manager.h"

#include <sys/stat.h>

#include <fstream>
#include <sstream>

#include "common/exception.h"

namespace minidb {

namespace {

// Row bytes can contain anything (including tabs/newlines), so hex-encode them.
std::string to_hex(const std::string& s) {
    static const char* d = "0123456789abcdef";
    std::string out;
    out.reserve(s.size() * 2);
    for (unsigned char c : s) { out += d[c >> 4]; out += d[c & 0xF]; }
    return out;
}
std::string from_hex(const std::string& h) {
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return 0;
    };
    std::string out;
    out.reserve(h.size() / 2);
    for (std::size_t i = 0; i + 1 < h.size(); i += 2)
        out += static_cast<char>((nib(h[i]) << 4) | nib(h[i + 1]));
    return out;
}

std::string serialize(const LogRecord& r) {
    std::ostringstream os;
    switch (r.type) {
        case LogType::PUT:    os << "P\t" << r.tx << '\t' << r.table << '\t' << r.key << '\t' << to_hex(r.row); break;
        case LogType::ERASE:  os << "E\t" << r.tx << '\t' << r.table << '\t' << r.key; break;
        case LogType::COMMIT: os << "C\t" << r.tx; break;
    }
    return os.str();
}

} // namespace

void LogManager::append(const LogRecord& rec) { buffer_.push_back(serialize(rec)); }

void LogManager::flush() {
    if (buffer_.empty()) return;
    std::ofstream out(path_, std::ios::app);
    if (!out) throw DBException("LogManager: cannot open WAL " + path_);
    for (const auto& line : buffer_) out << line << '\n';
    out.flush();  // push to the OS; survives an in-process crash
    buffer_.clear();
}

void LogManager::truncate() {
    buffer_.clear();
    std::ofstream out(path_, std::ios::trunc);  // empty the file
}

bool LogManager::empty() const {
    struct stat st{};
    if (::stat(path_.c_str(), &st) != 0) return true;
    return st.st_size == 0 && buffer_.empty();
}

std::vector<LogRecord> LogManager::read_all() const {
    std::vector<LogRecord> recs;
    std::ifstream in(path_);
    if (!in) return recs;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::istringstream is(line);
        std::string tag;
        std::getline(is, tag, '\t');
        LogRecord r;
        if (tag == "P") {
            std::string txs, keys, hex;
            std::getline(is, txs, '\t');
            std::getline(is, r.table, '\t');
            std::getline(is, keys, '\t');
            std::getline(is, hex, '\t');
            r.type = LogType::PUT;
            r.tx = std::stoull(txs);
            r.key = std::stoll(keys);
            r.row = from_hex(hex);
        } else if (tag == "E") {
            std::string txs, keys;
            std::getline(is, txs, '\t');
            std::getline(is, r.table, '\t');
            std::getline(is, keys, '\t');
            r.type = LogType::ERASE;
            r.tx = std::stoull(txs);
            r.key = std::stoll(keys);
        } else if (tag == "C") {
            std::string txs;
            std::getline(is, txs, '\t');
            r.type = LogType::COMMIT;
            r.tx = std::stoull(txs);
        } else {
            continue;
        }
        recs.push_back(std::move(r));
    }
    return recs;
}

} // namespace minidb
