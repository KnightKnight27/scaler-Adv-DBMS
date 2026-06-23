#pragma once
#include "types.h"
#include <unordered_map>
#include <vector>
#include <optional>
#include <iostream>

class MVCCStore {
public:
    void write(const RecordKey& key, const std::string& value, TxnId txn_id, Timestamp begin_ts);
    std::optional<std::string> read(const RecordKey& key, Timestamp read_ts) const;
    void abort_txn(TxnId txn_id);
    void print_versions(const RecordKey& key) const;

private:
    std::unordered_map<RecordKey, std::vector<Version>> store;
};
