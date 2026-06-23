#include "mvcc.h"

namespace minidb {

void MVCCStore::put(int key, const std::string& value, int txn) {
    chains_[key].push_back(Version{value, txn});
    committed_.emplace(txn, false);  // not visible until commit()
}

bool MVCCStore::read(int key, int snapshot, std::string& out) const {
    auto it = chains_.find(key);
    if (it == chains_.end()) return false;
    // Walk newest -> oldest and return the first version that is committed and
    // whose creator id is within our snapshot.
    const std::vector<Version>& chain = it->second;
    for (auto v = chain.rbegin(); v != chain.rend(); ++v) {
        auto c = committed_.find(v->txn);
        bool isCommitted = (c != committed_.end() && c->second);
        if (isCommitted && v->txn <= snapshot) {
            out = v->value;
            return true;
        }
    }
    return false;
}

int MVCCStore::versionCount(int key) const {
    auto it = chains_.find(key);
    return it == chains_.end() ? 0 : static_cast<int>(it->second.size());
}

}  // namespace minidb
