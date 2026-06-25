#pragma once
#ifndef MVCC_H
#define MVCC_H

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

using VersionID = int;
using TransactionID = int;

struct Version {
    VersionID version_id;
    TransactionID created_by;
    TransactionID created_at_timestamp;
    std::unordered_map<std::string, std::string> data;
    bool is_deleted;

    Version(VersionID id, TransactionID tx_id, TransactionID ts)
        : version_id(id), created_by(tx_id), created_at_timestamp(ts), is_deleted(false) {}
};

class VersionChain {
private:
    std::vector<std::shared_ptr<Version>> versions;
    VersionID next_version_id;

public:
    VersionChain() : next_version_id(0) {}

    std::shared_ptr<Version> createVersion(TransactionID tx_id, TransactionID timestamp) {
        auto version = std::make_shared<Version>(next_version_id++, tx_id, timestamp);
        versions.push_back(version);
        return version;
    }

    std::shared_ptr<Version> getLatestVersion() {
        if (versions.empty()) return nullptr;
        return versions.back();
    }

    std::shared_ptr<Version> getVersionAt(TransactionID timestamp) {
        for (auto it = versions.rbegin(); it != versions.rend(); ++it) {
            if ((*it)->created_at_timestamp <= timestamp && !(*it)->is_deleted) {
                return *it;
            }
        }
        return nullptr;
    }

    void removeLastVersion() {
        if (!versions.empty()) {
            versions.pop_back();
        }
    }

    const std::vector<std::shared_ptr<Version>>& getAllVersions() const {
        return versions;
    }
};

#endif // MVCC_H
