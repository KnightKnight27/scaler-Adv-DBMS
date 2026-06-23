#include "minidb.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace minidb {
namespace {

std::string trim(std::string value) {
    auto left = std::find_if_not(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c); });
    auto right = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) { return std::isspace(c); }).base();
    if (left >= right) return "";
    return std::string(left, right);
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::vector<std::string> split(const std::string& text, char delimiter) {
    std::vector<std::string> parts;
    std::stringstream ss(text);
    std::string item;
    while (std::getline(ss, item, delimiter)) parts.push_back(trim(item));
    return parts;
}

std::vector<std::string> words(const std::string& text) {
    std::stringstream ss(text);
    std::vector<std::string> out;
    std::string item;
    while (ss >> item) out.push_back(item);
    return out;
}

std::string stripSemicolon(std::string sql) {
    sql = trim(sql);
    if (!sql.empty() && sql.back() == ';') sql.pop_back();
    return trim(sql);
}

std::string joinValues(const std::vector<std::string>& values, char delimiter) {
    std::ostringstream out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i) out << delimiter;
        out << values[i];
    }
    return out.str();
}

int toInt(const std::string& value) {
    return std::stoi(value);
}

std::string unqualify(std::string column) {
    auto dot = column.find('.');
    if (dot != std::string::npos) column = column.substr(dot + 1);
    return trim(column);
}

std::string qualifier(const std::string& column) {
    auto dot = column.find('.');
    if (dot == std::string::npos) return "";
    return lower(trim(column.substr(0, dot)));
}

bool compareValues(const std::string& left, const std::string& op, const std::string& right) {
    const bool leftNumeric = !left.empty() && std::all_of(left.begin(), left.end(), [](unsigned char c) { return std::isdigit(c) || c == '-'; });
    const bool rightNumeric = !right.empty() && std::all_of(right.begin(), right.end(), [](unsigned char c) { return std::isdigit(c) || c == '-'; });
    if (leftNumeric && rightNumeric) {
        int a = toInt(left);
        int b = toInt(right);
        if (op == "=") return a == b;
        if (op == ">") return a > b;
        if (op == "<") return a < b;
    }
    if (op == "=") return left == right;
    if (op == ">") return left > right;
    if (op == "<") return left < right;
    return false;
}

std::size_t columnIndex(const TableSchema& schema, const std::string& name) {
    const std::string wanted = lower(unqualify(name));
    for (std::size_t i = 0; i < schema.columns.size(); ++i) {
        if (lower(schema.columns[i]) == wanted) return i;
    }
    throw std::runtime_error("unknown column: " + name);
}

std::size_t activeRowCount(const std::vector<Row>& rows) {
    return static_cast<std::size_t>(std::count_if(rows.begin(), rows.end(), [](const Row& row) { return !row.deleted; }));
}

std::string formatRatio(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(2) << value;
    return out.str();
}

double estimateSelectivity(
    const TableSchema& schema,
    const std::vector<Row>& rows,
    const std::optional<std::tuple<std::string, std::string, std::string>>& where,
    const std::string& tableName) {
    if (!where) return 1.0;
    auto [colName, op, value] = *where;
    const std::string q = qualifier(colName);
    if (!q.empty() && q != tableName) return 1.0;
    const std::string ucol = unqualify(colName);
    if (std::find(schema.columns.begin(), schema.columns.end(), ucol) == schema.columns.end()) return 1.0;

    const std::size_t total = activeRowCount(rows);
    if (total == 0) return 0.0;
    const std::size_t col = columnIndex(schema, ucol);
    std::size_t matches = 0;
    for (const auto& row : rows) {
        if (!row.deleted && compareValues(row.values[col], op, value)) ++matches;
    }
    return static_cast<double>(matches) / static_cast<double>(total);
}

std::string encodeRow(const Row& row) {
    return std::string(row.deleted ? "1|" : "0|") + joinValues(row.values, ',');
}

Row decodeRow(const std::string& record) {
    Row row;
    auto sep = record.find('|');
    if (sep == std::string::npos) throw std::runtime_error("bad row record: " + record);
    row.deleted = record.substr(0, sep) == "1";
    row.values = split(record.substr(sep + 1), ',');
    return row;
}

} // namespace

BPlusTree::BPlusTree(std::size_t order) : order_(std::max<std::size_t>(4, order)), root_(makeNode(true)) {}

BPlusTree::Node* BPlusTree::makeNode(bool leaf) {
    ownedNodes_.push_back(new Node());
    ownedNodes_.back()->leaf = leaf;
    return ownedNodes_.back();
}

void BPlusTree::splitChild(Node* parent, std::size_t childIndex) {
    Node* child = parent->children[childIndex];
    Node* sibling = makeNode(child->leaf);
    const std::size_t mid = child->keys.size() / 2;

    if (child->leaf) {
        sibling->keys.assign(child->keys.begin() + mid, child->keys.end());
        sibling->rowIds.assign(child->rowIds.begin() + mid, child->rowIds.end());
        child->keys.resize(mid);
        child->rowIds.resize(mid);
        sibling->next = child->next;
        child->next = sibling;
        parent->keys.insert(parent->keys.begin() + static_cast<long long>(childIndex), sibling->keys.front());
        parent->children.insert(parent->children.begin() + static_cast<long long>(childIndex + 1), sibling);
    } else {
        int promoted = child->keys[mid];
        sibling->keys.assign(child->keys.begin() + static_cast<long long>(mid + 1), child->keys.end());
        sibling->children.assign(child->children.begin() + static_cast<long long>(mid + 1), child->children.end());
        child->keys.resize(mid);
        child->children.resize(mid + 1);
        parent->keys.insert(parent->keys.begin() + static_cast<long long>(childIndex), promoted);
        parent->children.insert(parent->children.begin() + static_cast<long long>(childIndex + 1), sibling);
    }
}

void BPlusTree::insertNonFull(Node* node, int key, std::size_t rowId) {
    if (node->leaf) {
        auto pos = std::lower_bound(node->keys.begin(), node->keys.end(), key);
        auto index = static_cast<std::size_t>(pos - node->keys.begin());
        if (pos != node->keys.end() && *pos == key) {
            node->rowIds[index] = rowId;
            return;
        }
        node->keys.insert(pos, key);
        node->rowIds.insert(node->rowIds.begin() + static_cast<long long>(index), rowId);
        return;
    }

    auto childIndex = static_cast<std::size_t>(std::upper_bound(node->keys.begin(), node->keys.end(), key) - node->keys.begin());
    if (node->children[childIndex]->keys.size() >= order_ - 1) {
        splitChild(node, childIndex);
        if (key >= node->keys[childIndex]) ++childIndex;
    }
    insertNonFull(node->children[childIndex], key, rowId);
}

void BPlusTree::insert(int key, std::size_t rowId) {
    if (root_->keys.size() >= order_ - 1) {
        Node* oldRoot = root_;
        root_ = makeNode(false);
        root_->children.push_back(oldRoot);
        splitChild(root_, 0);
    }
    insertNonFull(root_, key, rowId);
}

void BPlusTree::remove(int key) {
    Node* node = root_;
    while (!node->leaf) {
        auto childIndex = static_cast<std::size_t>(std::upper_bound(node->keys.begin(), node->keys.end(), key) - node->keys.begin());
        node = node->children[childIndex];
    }
    auto pos = std::lower_bound(node->keys.begin(), node->keys.end(), key);
    if (pos == node->keys.end() || *pos != key) return;
    auto index = static_cast<std::size_t>(pos - node->keys.begin());
    node->keys.erase(pos);
    node->rowIds.erase(node->rowIds.begin() + static_cast<long long>(index));
}

std::optional<std::size_t> BPlusTree::search(int key) const {
    Node* node = root_;
    while (!node->leaf) {
        auto childIndex = static_cast<std::size_t>(std::upper_bound(node->keys.begin(), node->keys.end(), key) - node->keys.begin());
        node = node->children[childIndex];
    }
    auto pos = std::lower_bound(node->keys.begin(), node->keys.end(), key);
    if (pos == node->keys.end() || *pos != key) return std::nullopt;
    return node->rowIds[static_cast<std::size_t>(pos - node->keys.begin())];
}

std::vector<int> BPlusTree::keys() const {
    Node* node = root_;
    while (!node->leaf) node = node->children.front();
    std::vector<int> out;
    while (node) {
        out.insert(out.end(), node->keys.begin(), node->keys.end());
        node = node->next;
    }
    return out;
}

BufferPool::BufferPool(std::size_t capacity) : capacity_(std::max<std::size_t>(1, capacity)) {}

BufferPool::Frame& BufferPool::pinFrame(const std::string& path, std::size_t pageId) {
    for (auto& frame : frames_) {
        if (frame.path == path && frame.pageId == pageId) {
            frame.reference = true;
            return frame;
        }
    }

    if (frames_.size() < capacity_) {
        frames_.push_back(Frame{path, pageId, "", false, true});
        return frames_.back();
    }

    // Small CLOCK replacement policy: pages get one second chance before eviction.
    while (true) {
        Frame& frame = frames_[hand_];
        if (!frame.reference) {
            flush(frame);
            frame = Frame{path, pageId, "", false, true};
            hand_ = (hand_ + 1) % frames_.size();
            return frames_[(hand_ + frames_.size() - 1) % frames_.size()];
        }
        frame.reference = false;
        hand_ = (hand_ + 1) % frames_.size();
    }
}

std::string BufferPool::readPage(const std::string& path, std::size_t pageId) {
    Frame& frame = pinFrame(path, pageId);
    if (!frame.data.empty()) return frame.data;
    std::ifstream in(path, std::ios::binary);
    if (!in) return "";
    in.seekg(static_cast<std::streamoff>(pageId * PageManager::pageSize));
    std::string data(PageManager::pageSize, '\0');
    in.read(data.data(), static_cast<std::streamsize>(data.size()));
    data.resize(static_cast<std::size_t>(in.gcount()));
    frame.data = data;
    return frame.data;
}

void BufferPool::writePage(const std::string& path, std::size_t pageId, const std::string& data) {
    Frame& frame = pinFrame(path, pageId);
    frame.data = data;
    frame.dirty = true;
}

void BufferPool::flush(Frame& frame) {
    if (!frame.dirty) return;
    std::filesystem::create_directories(std::filesystem::path(frame.path).parent_path());
    std::fstream out(frame.path, std::ios::in | std::ios::out | std::ios::binary);
    if (!out) out.open(frame.path, std::ios::out | std::ios::binary);
    out.seekp(static_cast<std::streamoff>(frame.pageId * PageManager::pageSize));
    out.write(frame.data.data(), static_cast<std::streamsize>(frame.data.size()));
    frame.dirty = false;
}

void BufferPool::flushAll() {
    for (auto& frame : frames_) flush(frame);
}

PageManager::PageManager(std::string dataDir, std::size_t bufferPages)
    : dataDir_(std::move(dataDir)), bufferPool_(bufferPages) {
    std::filesystem::create_directories(dataDir_);
}

std::string PageManager::tablePath(const std::string& tableName) const {
    return (std::filesystem::path(dataDir_) / (tableName + ".heap")).string();
}

void PageManager::writeRecords(const std::string& tableName, const std::vector<std::string>& records) {
    const std::string path = tablePath(tableName);
    std::filesystem::remove(path);
    std::string page;
    std::size_t pageId = 0;
    for (const auto& record : records) {
        std::string line = record + "\n";
        if (page.size() + line.size() > pageSize) {
            bufferPool_.writePage(path, pageId++, page);
            page.clear();
        }
        page += line;
    }
    bufferPool_.writePage(path, pageId, page);
    bufferPool_.flushAll();
}

std::vector<std::string> PageManager::readRecords(const std::string& tableName) {
    const std::string path = tablePath(tableName);
    std::vector<std::string> out;
    if (!std::filesystem::exists(path)) return out;
    const auto fileSize = std::filesystem::file_size(path);
    const std::size_t pages = (fileSize + pageSize - 1) / pageSize;
    for (std::size_t i = 0; i < pages; ++i) {
        std::stringstream ss(bufferPool_.readPage(path, i));
        std::string line;
        while (std::getline(ss, line)) {
            line = trim(line);
            if (!line.empty()) out.push_back(line);
        }
    }
    return out;
}

void PageManager::flush() {
    bufferPool_.flushAll();
}

Wal::Wal(std::string path) : path_(std::move(path)) {
    std::filesystem::create_directories(std::filesystem::path(path_).parent_path());
}

void Wal::append(const std::string& record) {
    std::ofstream out(path_, std::ios::app);
    out << record << "\n";
}

std::vector<std::string> Wal::readAll() const {
    std::vector<std::string> records;
    std::ifstream in(path_);
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (!line.empty()) records.push_back(line);
    }
    return records;
}

bool LockManager::acquireShared(int txId, const std::string& resource) {
    auto& state = locks_[resource];
    if (state.exclusiveOwner && *state.exclusiveOwner != txId) {
        recordWait(txId, {*state.exclusiveOwner});
        return false;
    }
    waitsFor_.erase(txId);
    state.sharedOwners.insert(txId);
    return true;
}

bool LockManager::acquireExclusive(int txId, const std::string& resource) {
    auto& state = locks_[resource];
    std::set<int> blockers;
    if (state.exclusiveOwner && *state.exclusiveOwner != txId) blockers.insert(*state.exclusiveOwner);
    for (int owner : state.sharedOwners) {
        if (owner != txId) blockers.insert(owner);
    }
    if (!blockers.empty()) {
        recordWait(txId, blockers);
        return false;
    }
    waitsFor_.erase(txId);
    state.sharedOwners.erase(txId);
    state.exclusiveOwner = txId;
    return true;
}

void LockManager::releaseAll(int txId) {
    for (auto& [_, state] : locks_) {
        state.sharedOwners.erase(txId);
        if (state.exclusiveOwner && *state.exclusiveOwner == txId) state.exclusiveOwner.reset();
    }
    waitsFor_.erase(txId);
    for (auto& [_, blockers] : waitsFor_) blockers.erase(txId);
}

void LockManager::recordWait(int txId, const std::set<int>& blockers) {
    waitsFor_[txId] = blockers;
}

bool LockManager::hasPath(int fromTx, int targetTx, std::set<int>& visited) const {
    if (fromTx == targetTx) return true;
    if (!visited.insert(fromTx).second) return false;
    auto it = waitsFor_.find(fromTx);
    if (it == waitsFor_.end()) return false;
    for (int next : it->second) {
        if (hasPath(next, targetTx, visited)) return true;
    }
    return false;
}

bool LockManager::deadlockDetected() const {
    for (const auto& [txId, blockers] : waitsFor_) {
        for (int blocker : blockers) {
            std::set<int> visited;
            if (hasPath(blocker, txId, visited)) return true;
        }
    }
    return false;
}

std::string LockManager::state() const {
    std::ostringstream out;
    for (const auto& [resource, lock] : locks_) {
        out << resource << " S{";
        bool first = true;
        for (int owner : lock.sharedOwners) {
            if (!first) out << ",";
            out << owner;
            first = false;
        }
        out << "} X{";
        if (lock.exclusiveOwner) out << *lock.exclusiveOwner;
        out << "} ";
    }
    if (!waitsFor_.empty()) {
        out << "waits{";
        bool firstTx = true;
        for (const auto& [txId, blockers] : waitsFor_) {
            if (!firstTx) out << "; ";
            out << txId << "->";
            bool firstBlocker = true;
            for (int blocker : blockers) {
                if (!firstBlocker) out << ",";
                out << blocker;
                firstBlocker = false;
            }
            firstTx = false;
        }
        out << "} ";
    }
    return out.str();
}

MiniDB::MiniDB(std::string dataDir)
    : dataDir_(std::move(dataDir)),
      pages_(dataDir_),
      wal_((std::filesystem::path(dataDir_) / "minidb.wal").string()) {
    recover();
}

QueryResult MiniDB::execute(const std::string& rawSql) {
    const std::string sql = stripSemicolon(rawSql);
    const std::string lsql = lower(sql);
    if (sql.empty()) return QueryResult{{}, {}, ""};
    if (lsql == "begin") return begin();
    if (lsql == "commit") return commit();
    if (lsql == "rollback") return rollback();
    if (lsql.rfind("create table ", 0) == 0) return createTable(sql);
    if (lsql.rfind("insert into ", 0) == 0) return insertInto(sql);
    if (lsql.rfind("select ", 0) == 0) return selectFrom(sql);
    if (lsql.rfind("delete from ", 0) == 0) return deleteFrom(sql);
    if (lsql == "lock_demo") return lockDemo();
    if (lsql == "perf_demo") return perfDemo();
    if (lsql == "storage_demo") return storageDemo();
    if (lsql.rfind("index_demo ", 0) == 0) return indexDemo(sql);
    throw std::runtime_error("unsupported SQL: " + sql);
}

QueryResult MiniDB::lockDemo() {
    LockManager demoLocks;
    const bool tx1Users = demoLocks.acquireExclusive(1, "table:users");
    const bool tx2Orders = demoLocks.acquireExclusive(2, "table:orders");
    const bool tx1WaitsOrders = demoLocks.acquireExclusive(1, "table:orders");
    const bool tx2WaitsUsers = demoLocks.acquireExclusive(2, "table:users");

    QueryResult result;
    result.columns = {"step", "result"};
    result.rows.push_back({"T1 X lock users", tx1Users ? "granted" : "blocked"});
    result.rows.push_back({"T2 X lock orders", tx2Orders ? "granted" : "blocked"});
    result.rows.push_back({"T1 requests orders", tx1WaitsOrders ? "granted" : "blocked by T2"});
    result.rows.push_back({"T2 requests users", tx2WaitsUsers ? "granted" : "blocked by T1"});
    result.rows.push_back({"deadlock", demoLocks.deadlockDetected() ? "detected" : "not detected"});
    result.message = demoLocks.state();
    return result;
}

QueryResult MiniDB::perfDemo() {
    std::vector<Row> rows;
    rows.reserve(20000);
    for (int i = 0; i < 20000; ++i) {
        rows.push_back(Row{false, {std::to_string(i), "user" + std::to_string(i), std::to_string(i % 100)}});
    }

    auto rowAtATimeStart = std::chrono::steady_clock::now();
    std::size_t rowMatches = 0;
    for (const auto& row : rows) {
        if (!row.deleted && toInt(row.values[2]) > 70) ++rowMatches;
    }
    auto rowAtATimeEnd = std::chrono::steady_clock::now();

    auto batchStart = std::chrono::steady_clock::now();
    std::size_t batchMatches = 0;
    constexpr std::size_t batchSize = 128;
    for (std::size_t start = 0; start < rows.size(); start += batchSize) {
        const std::size_t end = std::min(rows.size(), start + batchSize);
        // Track A extension: evaluate a chunk at a time so predicate work is grouped.
        for (std::size_t i = start; i < end; ++i) {
            if (!rows[i].deleted && toInt(rows[i].values[2]) > 70) ++batchMatches;
        }
    }
    auto batchEnd = std::chrono::steady_clock::now();

    const auto rowMicros = std::chrono::duration_cast<std::chrono::microseconds>(rowAtATimeEnd - rowAtATimeStart).count();
    const auto batchMicros = std::chrono::duration_cast<std::chrono::microseconds>(batchEnd - batchStart).count();

    QueryResult result;
    result.columns = {"method", "rows", "matches", "micros"};
    result.rows.push_back({"row_at_a_time", std::to_string(rows.size()), std::to_string(rowMatches), std::to_string(rowMicros)});
    result.rows.push_back({"batch_128", std::to_string(rows.size()), std::to_string(batchMatches), std::to_string(batchMicros)});
    result.message = "Track A performance demo: batch processing compared with row-at-a-time scan";
    return result;
}

QueryResult MiniDB::storageDemo() {
    pages_.flush();
    QueryResult result;
    result.columns = {"table", "heap_file", "page_size", "pages", "active_rows", "deleted_rows"};

    for (const auto& [tableName, table] : tables_) {
        const std::string path = pages_.tablePath(tableName);
        const std::uintmax_t bytes = std::filesystem::exists(path) ? std::filesystem::file_size(path) : 0;
        const std::uintmax_t pageCount = bytes == 0 ? 0 : (bytes + PageManager::pageSize - 1) / PageManager::pageSize;
        const std::size_t activeRows = activeRowCount(table.rows);
        const std::size_t deletedRows = table.rows.size() - activeRows;
        result.rows.push_back({
            tableName,
            path,
            std::to_string(PageManager::pageSize),
            std::to_string(pageCount),
            std::to_string(activeRows),
            std::to_string(deletedRows),
        });
    }

    result.message = "Storage demo: heap files are split into fixed-size pages and accessed through the buffer pool";
    return result;
}

QueryResult MiniDB::indexDemo(const std::string& sql) {
    const auto parts = words(sql);
    if (parts.size() != 2) throw std::runtime_error("expected INDEX_DEMO table");
    const std::string tableName = lower(parts[1]);
    ensureLoaded(tableName);
    const Table& table = tables_.at(tableName);

    QueryResult result;
    result.columns = {"primary_key", "row_id", "deleted"};
    for (int key : table.primaryIndex.keys()) {
        auto rowId = table.primaryIndex.search(key);
        if (!rowId) continue;
        result.rows.push_back({
            std::to_string(key),
            std::to_string(*rowId),
            table.rows[*rowId].deleted ? "true" : "false",
        });
    }
    result.message = "Index demo: primary-key B+ tree leaf keys in sorted order";
    return result;
}

QueryResult MiniDB::begin() {
    if (currentTx_ != 0) throw std::runtime_error("transaction already active");
    currentTx_ = nextTx_++;
    explicitTx_ = true;
    wal_.append("BEGIN|" + std::to_string(currentTx_));
    return QueryResult{{}, {}, "BEGIN tx=" + std::to_string(currentTx_)};
}

QueryResult MiniDB::commit() {
    if (currentTx_ == 0) throw std::runtime_error("no active transaction");
    const int txId = currentTx_;
    for (const auto& op : pending_[txId]) {
        if (op.type == "INSERT") {
            Table& table = tables_.at(op.table);
            table.rows.push_back(op.row);
            table.primaryIndex.insert(toInt(op.row.values[table.schema.primaryKeyColumn]), table.rows.size() - 1);
        } else if (op.type == "DELETE") {
            Table& table = tables_.at(op.table);
            table.rows[op.rowId].deleted = true;
            table.primaryIndex.remove(toInt(table.rows[op.rowId].values[table.schema.primaryKeyColumn]));
        }
    }
    wal_.append("COMMIT|" + std::to_string(txId));
    std::set<std::string> touched;
    for (const auto& op : pending_[txId]) touched.insert(op.table);
    for (const auto& tableName : touched) persist(tableName);
    pending_.erase(txId);
    locks_.releaseAll(txId);
    currentTx_ = 0;
    explicitTx_ = false;
    return QueryResult{{}, {}, "COMMIT tx=" + std::to_string(txId)};
}

QueryResult MiniDB::rollback() {
    if (currentTx_ == 0) throw std::runtime_error("no active transaction");
    const int txId = currentTx_;
    wal_.append("ROLLBACK|" + std::to_string(txId));
    pending_.erase(txId);
    locks_.releaseAll(txId);
    currentTx_ = 0;
    explicitTx_ = false;
    return QueryResult{{}, {}, "ROLLBACK tx=" + std::to_string(txId)};
}

int MiniDB::txForStatement() {
    if (currentTx_ != 0) return currentTx_;
    currentTx_ = nextTx_++;
    wal_.append("BEGIN|" + std::to_string(currentTx_));
    return currentTx_;
}

void MiniDB::autocommitIfNeeded(int txId) {
    if (currentTx_ == txId && !explicitTx_) {
        commit();
    }
}

QueryResult MiniDB::createTable(const std::string& sql) {
    auto open = sql.find('(');
    auto close = sql.rfind(')');
    if (open == std::string::npos || close == std::string::npos || close <= open) {
        throw std::runtime_error("expected CREATE TABLE name (col1, col2, ...)");
    }
    std::string prefix = trim(sql.substr(0, open));
    auto w = words(prefix);
    if (w.size() != 3) throw std::runtime_error("expected CREATE TABLE name");
    const std::string tableName = lower(w[2]);
    Table table;
    table.schema.name = tableName;
    table.schema.columns = split(sql.substr(open + 1, close - open - 1), ',');
    table.schema.primaryKeyColumn = 0;
    tables_[tableName] = table;
    wal_.append("CREATE|" + tableName + "|" + joinValues(tables_[tableName].schema.columns, ','));
    persist(tableName);
    return QueryResult{{}, {}, "created table " + tableName};
}

void MiniDB::ensureLoaded(const std::string& tableName) {
    if (tables_.count(tableName)) return;
    const auto records = pages_.readRecords(tableName);
    if (records.empty()) throw std::runtime_error("unknown table: " + tableName);

    Table table;
    table.schema.name = tableName;
    table.schema.columns = split(records.front().substr(std::string("schema|").size()), ',');
    for (std::size_t i = 1; i < records.size(); ++i) {
        Row row = decodeRow(records[i]);
        if (!row.deleted) table.primaryIndex.insert(toInt(row.values[table.schema.primaryKeyColumn]), table.rows.size());
        table.rows.push_back(row);
    }
    tables_[tableName] = table;
}

void MiniDB::persist(const std::string& tableName) {
    const Table& table = tables_.at(tableName);
    std::vector<std::string> records;
    records.push_back("schema|" + joinValues(table.schema.columns, ','));
    for (const auto& row : table.rows) records.push_back(encodeRow(row));
    pages_.writeRecords(tableName, records);
}

void MiniDB::applyInsert(int txId, const std::string& tableName, const Row& row) {
    wal_.append("INSERT|" + std::to_string(txId) + "|" + tableName + "|" + joinValues(row.values, ','));
    pending_[txId].push_back(PendingOp{tableName, "INSERT", row, 0});
}

void MiniDB::applyDelete(int txId, const std::string& tableName, std::size_t rowId) {
    const Table& table = tables_.at(tableName);
    const std::string pk = table.rows[rowId].values[table.schema.primaryKeyColumn];
    wal_.append("DELETE|" + std::to_string(txId) + "|" + tableName + "|" + pk);
    pending_[txId].push_back(PendingOp{tableName, "DELETE", Row{}, rowId});
}

bool MiniDB::pendingInsertHasPrimaryKey(int txId, const std::string& tableName, int primaryKey) const {
    auto it = pending_.find(txId);
    if (it == pending_.end()) return false;
    const Table& table = tables_.at(tableName);
    for (const auto& op : it->second) {
        if (op.type == "INSERT" && op.table == tableName && toInt(op.row.values[table.schema.primaryKeyColumn]) == primaryKey) {
            return true;
        }
    }
    return false;
}

QueryResult MiniDB::insertInto(const std::string& sql) {
    auto lsql = lower(sql);
    auto valuesPos = lsql.find(" values ");
    if (valuesPos == std::string::npos) throw std::runtime_error("expected INSERT INTO table VALUES (...)");
    std::string tableName = lower(trim(sql.substr(std::string("insert into ").size(), valuesPos - std::string("insert into ").size())));
    ensureLoaded(tableName);

    auto open = sql.find('(', valuesPos);
    auto close = sql.rfind(')');
    if (open == std::string::npos || close == std::string::npos || close <= open) throw std::runtime_error("expected VALUES (...)");
    Row row{false, split(sql.substr(open + 1, close - open - 1), ',')};
    Table& table = tables_.at(tableName);
    if (row.values.size() != table.schema.columns.size()) throw std::runtime_error("column count mismatch");

    int txId = txForStatement();
    if (!locks_.acquireExclusive(txId, "table:" + tableName)) throw std::runtime_error("lock conflict on " + tableName);
    const int pk = toInt(row.values[table.schema.primaryKeyColumn]);
    if (table.primaryIndex.search(pk) || pendingInsertHasPrimaryKey(txId, tableName, pk)) {
        throw std::runtime_error("duplicate primary key: " + std::to_string(pk));
    }
    applyInsert(txId, tableName, row);
    autocommitIfNeeded(txId);
    return QueryResult{{}, {}, "inserted into " + tableName};
}

QueryResult MiniDB::deleteFrom(const std::string& sql) {
    const auto w = words(sql);
    if (w.size() < 7 || lower(w[0]) != "delete" || lower(w[1]) != "from" || lower(w[3]) != "where") {
        throw std::runtime_error("expected DELETE FROM table WHERE column op value");
    }
    const std::string tableName = lower(w[2]);
    ensureLoaded(tableName);
    Table& table = tables_.at(tableName);

    int txId = txForStatement();
    if (!locks_.acquireExclusive(txId, "table:" + tableName)) throw std::runtime_error("lock conflict on " + tableName);

    const std::string column = w[4];
    const std::string op = w[5];
    const std::string value = w[6];
    const std::size_t col = columnIndex(table.schema, column);
    std::size_t deleted = 0;

    // Primary-key equality delete uses the B+ tree, which is the path the optimizer should prefer.
    if (col == table.schema.primaryKeyColumn && op == "=") {
        auto rowId = table.primaryIndex.search(toInt(value));
        if (rowId && !table.rows[*rowId].deleted) {
            applyDelete(txId, tableName, *rowId);
            ++deleted;
        }
    } else {
        for (std::size_t i = 0; i < table.rows.size(); ++i) {
            if (!table.rows[i].deleted && compareValues(table.rows[i].values[col], op, value)) {
                applyDelete(txId, tableName, i);
                ++deleted;
            }
        }
    }
    autocommitIfNeeded(txId);
    return QueryResult{{}, {}, "deleted " + std::to_string(deleted) + " row(s)"};
}

QueryResult MiniDB::selectFrom(const std::string& sql) {
    const auto w = words(sql);
    if (w.size() < 4 || lower(w[0]) != "select" || lower(w[2]) != "from") {
        throw std::runtime_error("expected SELECT columns FROM table ...");
    }
    const std::vector<std::string> projected = split(w[1], ',');
    const std::string leftName = lower(w[3]);
    ensureLoaded(leftName);
    Table& left = tables_.at(leftName);

    int txId = txForStatement();
    if (!locks_.acquireShared(txId, "table:" + leftName)) throw std::runtime_error("lock conflict on " + leftName);

    bool hasJoin = false;
    std::string rightName;
    std::string joinLeft;
    std::string joinRight;
    std::size_t wherePos = 0;
    for (std::size_t i = 4; i < w.size(); ++i) {
        if (lower(w[i]) == "join") {
            hasJoin = true;
            rightName = lower(w[i + 1]);
            if (lower(w[i + 2]) != "on") throw std::runtime_error("expected JOIN table ON a = b");
            joinLeft = w[i + 3];
            joinRight = w[i + 5];
        }
        if (lower(w[i]) == "where") wherePos = i;
    }

    std::optional<std::tuple<std::string, std::string, std::string>> where;
    if (wherePos != 0) where = std::make_tuple(w[wherePos + 1], w[wherePos + 2], w[wherePos + 3]);

    QueryResult result;
    result.columns = projected;
    std::string plan = "plan: table scan";
    const double leftSelectivity = estimateSelectivity(left.schema, left.rows, where, leftName);

    if (!hasJoin) {
        std::vector<std::size_t> candidates;
        if (where) {
            auto [colName, op, value] = *where;
            const std::size_t col = columnIndex(left.schema, colName);
            if (col == left.schema.primaryKeyColumn && op == "=") {
                plan = "plan: primary B+ tree index scan";
                auto rowId = left.primaryIndex.search(toInt(value));
                if (rowId) candidates.push_back(*rowId);
            }
        }
        if (candidates.empty()) {
            for (std::size_t i = 0; i < left.rows.size(); ++i) candidates.push_back(i);
        }

        for (std::size_t rowId : candidates) {
            const Row& row = left.rows[rowId];
            if (row.deleted) continue;
            if (where) {
                auto [colName, op, value] = *where;
                if (!compareValues(row.values[columnIndex(left.schema, colName)], op, value)) continue;
            }
            std::vector<std::string> output;
            if (projected.size() == 1 && projected[0] == "*") {
                output = row.values;
                result.columns = left.schema.columns;
            } else {
                for (const auto& col : projected) output.push_back(row.values[columnIndex(left.schema, col)]);
            }
            result.rows.push_back(output);
        }
        result.message = plan + "; selectivity=" + formatRatio(leftSelectivity);
        autocommitIfNeeded(txId);
        return result;
    }

    ensureLoaded(rightName);
    if (!locks_.acquireShared(txId, "table:" + rightName)) throw std::runtime_error("lock conflict on " + rightName);
    Table& right = tables_.at(rightName);
    const std::size_t leftJoinCol = columnIndex(left.schema, joinLeft);
    const std::size_t rightJoinCol = columnIndex(right.schema, joinRight);
    const double rightSelectivity = estimateSelectivity(right.schema, right.rows, where, rightName);
    const double leftEstimatedRows = static_cast<double>(activeRowCount(left.rows)) * leftSelectivity;
    const double rightEstimatedRows = static_cast<double>(activeRowCount(right.rows)) * rightSelectivity;
    const bool rightFirst = rightEstimatedRows < leftEstimatedRows;

    plan = "plan: nested loop join";
    if (!rightFirst && rightJoinCol == right.schema.primaryKeyColumn) {
        plan = "plan: index nested loop join using right primary B+ tree";
    } else if (rightFirst && leftJoinCol == left.schema.primaryKeyColumn) {
        plan = "plan: index nested loop join using left primary B+ tree";
    }
    plan += "; join order=" + std::string(rightFirst ? rightName + " -> " + leftName : leftName + " -> " + rightName);
    plan += "; selectivity " + leftName + "=" + formatRatio(leftSelectivity) + ", " + rightName + "=" + formatRatio(rightSelectivity);

    auto rowPassesWhere = [&](const Row& lrow, const Row& rrow) {
        if (!where) return true;
        auto [colName, op, value] = *where;
        const std::string q = qualifier(colName);
        const std::string ucol = unqualify(colName);
        const bool fromRight = q == rightName || (q.empty() && std::find(right.schema.columns.begin(), right.schema.columns.end(), ucol) != right.schema.columns.end() && std::find(left.schema.columns.begin(), left.schema.columns.end(), ucol) == left.schema.columns.end());
        const Row& source = fromRight ? rrow : lrow;
        const TableSchema& schema = fromRight ? right.schema : left.schema;
        return compareValues(source.values[columnIndex(schema, ucol)], op, value);
    };

    auto emitJoinedRow = [&](const Row& lrow, const Row& rrow) {
        std::vector<std::string> output;
        if (projected.size() == 1 && projected[0] == "*") {
            result.columns = left.schema.columns;
            result.columns.insert(result.columns.end(), right.schema.columns.begin(), right.schema.columns.end());
            output = lrow.values;
            output.insert(output.end(), rrow.values.begin(), rrow.values.end());
        } else {
            for (const auto& col : projected) {
                const std::string q = qualifier(col);
                const std::string ucol = unqualify(col);
                auto leftIt = std::find(left.schema.columns.begin(), left.schema.columns.end(), ucol);
                const bool fromRight = q == rightName || (q.empty() && leftIt == left.schema.columns.end());
                if (!fromRight && leftIt != left.schema.columns.end()) {
                    output.push_back(lrow.values[static_cast<std::size_t>(leftIt - left.schema.columns.begin())]);
                } else {
                    output.push_back(rrow.values[columnIndex(right.schema, ucol)]);
                }
            }
        }
        result.rows.push_back(output);
    };

    if (!rightFirst) {
        for (const auto& lrow : left.rows) {
            if (lrow.deleted) continue;
            std::vector<std::size_t> rightIds;
            if (rightJoinCol == right.schema.primaryKeyColumn) {
                auto rowId = right.primaryIndex.search(toInt(lrow.values[leftJoinCol]));
                if (rowId) rightIds.push_back(*rowId);
            } else {
                for (std::size_t i = 0; i < right.rows.size(); ++i) rightIds.push_back(i);
            }
            for (std::size_t rightId : rightIds) {
                const Row& rrow = right.rows[rightId];
                if (rrow.deleted) continue;
                if (lrow.values[leftJoinCol] == rrow.values[rightJoinCol] && rowPassesWhere(lrow, rrow)) {
                    emitJoinedRow(lrow, rrow);
                }
            }
        }
    } else {
        for (const auto& rrow : right.rows) {
            if (rrow.deleted) continue;
            std::vector<std::size_t> leftIds;
            if (leftJoinCol == left.schema.primaryKeyColumn) {
                auto rowId = left.primaryIndex.search(toInt(rrow.values[rightJoinCol]));
                if (rowId) leftIds.push_back(*rowId);
            } else {
                for (std::size_t i = 0; i < left.rows.size(); ++i) leftIds.push_back(i);
            }
            for (std::size_t leftId : leftIds) {
                const Row& lrow = left.rows[leftId];
                if (lrow.deleted) continue;
                if (lrow.values[leftJoinCol] == rrow.values[rightJoinCol] && rowPassesWhere(lrow, rrow)) {
                    emitJoinedRow(lrow, rrow);
                }
            }
        }
    }
    result.message = plan;
    autocommitIfNeeded(txId);
    return result;
}

void MiniDB::recover() {
    tables_.clear();
    if (std::filesystem::exists(dataDir_)) {
        for (const auto& entry : std::filesystem::directory_iterator(dataDir_)) {
            if (entry.path().extension() == ".heap") {
                ensureLoaded(entry.path().stem().string());
            }
        }
    }
    replayWal();
    for (auto& [_, table] : tables_) rebuildIndex(table);
}

void MiniDB::rebuildIndex(Table& table) {
    table.primaryIndex = BPlusTree();
    for (std::size_t i = 0; i < table.rows.size(); ++i) {
        if (!table.rows[i].deleted) {
            table.primaryIndex.insert(toInt(table.rows[i].values[table.schema.primaryKeyColumn]), i);
        }
    }
}

void MiniDB::replayWal() {
    struct WalOp {
        std::string type;
        std::string table;
        Row row;
        std::string primaryKey;
    };

    std::unordered_map<int, std::vector<WalOp>> opsByTx;
    std::set<int> committed;
    std::set<int> rolledBack;
    std::set<std::string> touched;

    for (const auto& record : wal_.readAll()) {
        const auto parts = split(record, '|');
        if (parts.empty()) continue;
        if (parts[0] == "CREATE" && parts.size() >= 3) {
            const std::string tableName = lower(parts[1]);
            if (!tables_.count(tableName)) {
                Table table;
                table.schema.name = tableName;
                table.schema.columns = split(parts[2], ',');
                table.schema.primaryKeyColumn = 0;
                tables_[tableName] = table;
                touched.insert(tableName);
            }
        } else if (parts[0] == "INSERT" && parts.size() >= 4) {
            opsByTx[toInt(parts[1])].push_back(WalOp{"INSERT", lower(parts[2]), Row{false, split(parts[3], ',')}, ""});
        } else if (parts[0] == "DELETE" && parts.size() >= 4) {
            opsByTx[toInt(parts[1])].push_back(WalOp{"DELETE", lower(parts[2]), Row{}, parts[3]});
        } else if (parts[0] == "COMMIT" && parts.size() >= 2) {
            committed.insert(toInt(parts[1]));
        } else if (parts[0] == "ROLLBACK" && parts.size() >= 2) {
            rolledBack.insert(toInt(parts[1]));
        }
    }

    for (int txId : committed) {
        if (rolledBack.count(txId)) continue;
        for (const auto& op : opsByTx[txId]) {
            if (!tables_.count(op.table)) continue;
            Table& table = tables_.at(op.table);
            if (op.type == "INSERT") {
                const int pk = toInt(op.row.values[table.schema.primaryKeyColumn]);
                auto existing = table.primaryIndex.search(pk);
                if (!existing || table.rows[*existing].deleted) {
                    table.rows.push_back(op.row);
                    table.primaryIndex.insert(pk, table.rows.size() - 1);
                    touched.insert(op.table);
                }
            } else if (op.type == "DELETE") {
                const int pk = toInt(op.primaryKey);
                auto existing = table.primaryIndex.search(pk);
                if (existing && !table.rows[*existing].deleted) {
                    table.rows[*existing].deleted = true;
                    table.primaryIndex.remove(pk);
                    touched.insert(op.table);
                }
            }
        }
    }

    for (const auto& tableName : touched) persist(tableName);
}

QueryResult MiniDB::demo() {
    execute("CREATE TABLE users (id, name, age)");
    execute("CREATE TABLE orders (id, user_id, amount)");
    execute("INSERT INTO users VALUES (1, Ada, 31)");
    execute("INSERT INTO users VALUES (2, Grace, 28)");
    execute("INSERT INTO users VALUES (3, Linus, 42)");
    execute("INSERT INTO orders VALUES (10, 1, 200)");
    execute("INSERT INTO orders VALUES (11, 3, 450)");
    execute("INSERT INTO orders VALUES (12, 2, 50)");
    return execute("SELECT name,amount FROM users JOIN orders ON users.id = orders.user_id WHERE amount > 100");
}

std::string renderResult(const QueryResult& result) {
    std::ostringstream out;
    if (!result.columns.empty()) {
        for (std::size_t i = 0; i < result.columns.size(); ++i) {
            if (i) out << " | ";
            out << result.columns[i];
        }
        out << "\n";
    }
    for (const auto& row : result.rows) {
        for (std::size_t i = 0; i < row.size(); ++i) {
            if (i) out << " | ";
            out << row[i];
        }
        out << "\n";
    }
    if (!result.message.empty()) out << result.message << "\n";
    return out.str();
}

} // namespace minidb
