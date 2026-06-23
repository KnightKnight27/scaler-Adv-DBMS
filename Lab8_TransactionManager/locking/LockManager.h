#ifndef LOCKMANAGER_H
#define LOCKMANAGER_H

#include <iostream>
#include <unordered_map>
#include <unordered_set>

enum class LockType {
    SHARED,
    EXCLUSIVE
};

struct LockInfo {

    LockType type;

    std::unordered_set<int> holders;
};

class LockManager {
private:

    std::unordered_map<int, LockInfo> locks;

public:

    bool acquireSharedLock(
        int transactionId,
        int resourceId
    ) {

        auto it = locks.find(resourceId);

        if (it == locks.end()) {

            LockInfo info;
            info.type = LockType::SHARED;
            info.holders.insert(transactionId);

            locks[resourceId] = info;

            return true;
        }

        if (it->second.type == LockType::SHARED) {

            it->second.holders.insert(transactionId);

            return true;
        }

        if (it->second.holders.count(transactionId))
            return true;

        return false;
    }

    bool acquireExclusiveLock(
        int transactionId,
        int resourceId
    ) {

        auto it = locks.find(resourceId);

        if (it == locks.end()) {

            LockInfo info;
            info.type = LockType::EXCLUSIVE;
            info.holders.insert(transactionId);

            locks[resourceId] = info;

            return true;
        }

        if (it->second.holders.size() == 1 &&
            it->second.holders.count(transactionId)) {

            it->second.type =
                LockType::EXCLUSIVE;

            return true;
        }

        return false;
    }

    void releaseLocks(
        int transactionId
    ) {

        for (auto it = locks.begin();
             it != locks.end();) {

            it->second.holders.erase(
                transactionId
            );

            if (it->second.holders.empty()) {

                it =
                    locks.erase(it);
            }
            else {

                ++it;
            }
        }
    }

    void printLocks() {

        std::cout
            << "\nCurrent Locks:\n";

        for (const auto& entry : locks) {

            std::cout
                << "Resource "
                << entry.first
                << " : ";

            if (entry.second.type ==
                LockType::SHARED)
                std::cout << "SHARED ";
            else
                std::cout << "EXCLUSIVE ";

            std::cout
                << "[ ";

            for (int holder :
                 entry.second.holders) {

                std::cout
                    << "T"
                    << holder
                    << " ";
            }

            std::cout
                << "]\n";
        }
    }
};

#endif