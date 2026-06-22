#include "recovery/wal.h"
#include <filesystem>
#include <cstring>

WAL::WAL(const std::string& log_file) : log_file_(log_file) {
    std::filesystem::path p(log_file_);
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path());

    // Read existing records to set next_lsn_
    if (std::filesystem::exists(log_file_)) {
        auto records = ReadAll();
        for (auto& r : records) {
            if (r.lsn >= next_lsn_) next_lsn_ = r.lsn + 1;
        }
    }
    out_.open(log_file_, std::ios::binary | std::ios::app);
}

WAL::~WAL() {
    if (out_.is_open()) out_.close();
}

// ---- Append helpers ----

int WAL::AppendBegin(int txn_id) {
    WALRecord r; r.txn_id = txn_id; r.type = WALRecordType::BEGIN;
    return AppendRecord(r);
}

int WAL::AppendCommit(int txn_id) {
    WALRecord r; r.txn_id = txn_id; r.type = WALRecordType::COMMIT;
    int lsn = AppendRecord(r);
    Flush();  // commit must be durable
    return lsn;
}

int WAL::AppendAbort(int txn_id) {
    WALRecord r; r.txn_id = txn_id; r.type = WALRecordType::ABORT;
    return AppendRecord(r);
}

int WAL::AppendInsert(int txn_id, const std::string& table, const RID& rid,
                      const char* data, int size) {
    WALRecord r;
    r.txn_id = txn_id; r.type = WALRecordType::INSERT;
    r.table_name = table; r.rid = rid;
    r.after_image.assign(data, data + size);
    return AppendRecord(r);
}

int WAL::AppendDelete(int txn_id, const std::string& table, const RID& rid,
                      const char* before_data, int before_size) {
    WALRecord r;
    r.txn_id = txn_id; r.type = WALRecordType::DELETE_REC;
    r.table_name = table; r.rid = rid;
    r.before_image.assign(before_data, before_data + before_size);
    return AppendRecord(r);
}

int WAL::AppendUpdate(int txn_id, const std::string& table, const RID& rid,
                      const char* before_data, int before_size,
                      const char* after_data, int after_size) {
    WALRecord r;
    r.txn_id = txn_id; r.type = WALRecordType::UPDATE_REC;
    r.table_name = table; r.rid = rid;
    r.before_image.assign(before_data, before_data + before_size);
    r.after_image.assign(after_data, after_data + after_size);
    return AppendRecord(r);
}

int WAL::AppendCheckpoint() {
    WALRecord r; r.type = WALRecordType::CHECKPOINT;
    return AppendRecord(r);
}

void WAL::Flush() {
    std::lock_guard<std::mutex> lock(mu_);
    if (out_.is_open()) out_.flush();
}

void WAL::Truncate() {
    std::lock_guard<std::mutex> lock(mu_);
    out_.close();
    out_.open(log_file_, std::ios::binary | std::ios::trunc);
    next_lsn_ = 1;
}

// ---- Binary serialization ----
// Format: [lsn(4)][txn_id(4)][type(1)][table_len(2)][table][rid(6)][before_len(4)][before][after_len(4)][after]

int WAL::AppendRecord(const WALRecord& rec) {
    std::lock_guard<std::mutex> lock(mu_);
    WALRecord r = rec;
    r.lsn = next_lsn_++;
    WriteRecord(out_, r);
    return r.lsn;
}

void WAL::WriteRecord(std::ostream& os, const WALRecord& r) {
    os.write((char*)&r.lsn, 4);
    os.write((char*)&r.txn_id, 4);
    uint8_t type = (uint8_t)r.type;
    os.write((char*)&type, 1);

    uint16_t tlen = r.table_name.size();
    os.write((char*)&tlen, 2);
    os.write(r.table_name.data(), tlen);

    os.write((char*)&r.rid.page_id, 4);
    uint16_t slot = r.rid.slot_id;
    os.write((char*)&slot, 2);

    int blen = r.before_image.size();
    os.write((char*)&blen, 4);
    if (blen > 0) os.write(r.before_image.data(), blen);

    int alen = r.after_image.size();
    os.write((char*)&alen, 4);
    if (alen > 0) os.write(r.after_image.data(), alen);
}

WALRecord WAL::ReadRecord(std::istream& is) {
    WALRecord r;
    is.read((char*)&r.lsn, 4);
    is.read((char*)&r.txn_id, 4);
    uint8_t type; is.read((char*)&type, 1);
    r.type = (WALRecordType)type;

    uint16_t tlen; is.read((char*)&tlen, 2);
    r.table_name.resize(tlen);
    if (tlen > 0) is.read(r.table_name.data(), tlen);

    is.read((char*)&r.rid.page_id, 4);
    uint16_t slot; is.read((char*)&slot, 2);
    r.rid.slot_id = slot;

    int blen; is.read((char*)&blen, 4);
    if (blen > 0) { r.before_image.resize(blen); is.read(r.before_image.data(), blen); }

    int alen; is.read((char*)&alen, 4);
    if (alen > 0) { r.after_image.resize(alen); is.read(r.after_image.data(), alen); }

    return r;
}

std::vector<WALRecord> WAL::ReadAll() {
    std::vector<WALRecord> records;
    std::ifstream in(log_file_, std::ios::binary);
    if (!in.is_open()) return records;

    while (in.peek() != EOF) {
        try {
            records.push_back(ReadRecord(in));
        } catch (...) { break; }
    }
    return records;
}
