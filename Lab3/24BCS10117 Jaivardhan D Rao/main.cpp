// Clock Sweep Buffer Replacement
// ADBMS Lab 3 - 24BCS10117 Jaivardhan D Rao

#include <bits/stdc++.h>
using namespace std;
#define int long long

const int MAX_USAGE = 5;   // same cap as postgres BM_MAX_USAGE_COUNT

template<typename K, typename V>
class ClockSweep {
    struct Frame {
        K key{};
        V val{};
        int usage = 0;
        int pins  = 0;
        bool valid = false;
    };

    vector<Frame> frames;
    unordered_map<K, int> idx;
    int cap;
    int hand = 0;
    int hitCount = 0, missCount = 0;
    mutable mutex mu;

    // walk the hand till we find an unpinned frame with usage=0.
    // along the way, decrement usage on whatever we pass over.
    int pickVictim() {
        int n = frames.size();
        int tries = (MAX_USAGE + 1) * n + 1;   // can't loop forever
        for (int i = 0; i < tries; i++) {
            Frame& f = frames[hand];
            int cur = hand;
            hand = (hand + 1) % n;

            if (f.pins > 0) continue;             // pinned, skip
            if (f.usage > 0) { f.usage--; continue; }  // second chance
            return cur;
        }
        throw runtime_error("everything is pinned, can't evict");
    }

    void bump(Frame& f) {
        if (f.usage < MAX_USAGE) f.usage++;
    }

public:
    ClockSweep(int c) : cap(c) {
        if (c <= 0) throw invalid_argument("cap must be > 0");
        frames.reserve(c);
    }

    bool get(const K& key, V& out) {
        lock_guard<mutex> lk(mu);
        auto it = idx.find(key);
        if (it == idx.end()) {
            missCount++;
            cout << "  miss key=" << key << "\n";
            return false;
        }
        Frame& f = frames[it->second];
        bump(f);
        f.pins++;
        hitCount++;
        out = f.val;
        cout << "  hit  key=" << key << " usage=" << f.usage << " pin=" << f.pins << "\n";
        return true;
    }

    bool get(const K& key) {
        V dummy;
        return get(key, dummy);
    }

    void put(const K& key, const V& val) {
        lock_guard<mutex> lk(mu);

        // already there? just refresh it
        auto it = idx.find(key);
        if (it != idx.end()) {
            Frame& f = frames[it->second];
            bump(f);
            f.pins++;
            f.val = val;
            cout << "  repin key=" << key << " usage=" << f.usage << "\n";
            return;
        }

        // empty slot available
        if ((int)frames.size() < cap) {
            int slot = frames.size();
            frames.push_back(Frame{key, val, 1, 1, true});
            idx[key] = slot;
            cout << "  load key=" << key << " -> slot " << slot << "\n";
            return;
        }

        // pool full -> evict someone
        int v = pickVictim();
        Frame& f = frames[v];
        cout << "  evict key=" << f.key << " from slot " << v << "\n";
        idx.erase(f.key);
        f.key = key;
        f.val = val;
        f.usage = 1;
        f.pins = 1;
        f.valid = true;
        idx[key] = v;
        cout << "  load key=" << key << " -> slot " << v << "\n";
    }

    void unpin(const K& key) {
        lock_guard<mutex> lk(mu);
        auto it = idx.find(key);
        if (it == idx.end()) return;
        Frame& f = frames[it->second];
        if (f.pins > 0) f.pins--;
    }

    void dump(const string& tag = "") const {
        lock_guard<mutex> lk(mu);
        cout << "\n-- pool " << tag << " (hand=" << hand
             << " hits=" << hitCount << " misses=" << missCount << ")\n";
        for (int i = 0; i < (int)frames.size(); i++) {
            const Frame& f = frames[i];
            cout << "  [" << setw(2) << i << "]"
                 << (i == hand ? " <-hand " : "        ")
                 << " key=" << setw(3) << f.key
                 << " usage=" << f.usage
                 << " pin=" << f.pins << "\n";
        }
    }

    int hits()   const { lock_guard<mutex> lk(mu); return hitCount; }
    int misses() const { lock_guard<mutex> lk(mu); return missCount; }
    int size()   const { lock_guard<mutex> lk(mu); return frames.size(); }
};

static void header(const string& s) {
    cout << "\n==== " << s << " ====\n";
}

signed main() {
    ClockSweep<int, string> pool(4);

    header("1) fill the pool");
    pool.put(10, "p10");
    pool.put(20, "p20");
    pool.put(30, "p30");
    pool.put(40, "p40");
    pool.dump("after fill");

    // drop pins so eviction is allowed later
    for (int k : {10, 20, 30, 40}) pool.unpin(k);
    pool.dump("after unpin all");

    header("2) re-reference to bump usage");
    pool.get(10); pool.unpin(10);
    pool.get(10); pool.unpin(10);   // make 10 hot
    pool.get(20); pool.unpin(20);
    pool.dump("after refs");

    header("3) insert 50 -> sweep should evict the coldest");
    pool.put(50, "p50");
    pool.unpin(50);
    pool.dump("after 50");

    header("4) insert 60");
    pool.put(60, "p60");
    pool.unpin(60);
    pool.dump("after 60");

    header("5) pin 10, then insert 70 -> 10 must survive");
    pool.get(10);              // pinned, not unpinned yet
    pool.put(70, "p70");
    pool.unpin(70);
    pool.unpin(10);
    pool.dump("after 70");

    header("6) stats");
    cout << "  hits   = " << pool.hits() << "\n";
    cout << "  misses = " << pool.misses() << "\n";
    cout << "  size   = " << pool.size() << "\n";

    header("7) concurrency check");
    ClockSweep<int, string> shared(8);
    auto job = [&shared](int base) {
        for (int i = 0; i < 200; i++) {
            int k = base + (i % 16);
            shared.put(k, "v" + to_string(k));
            shared.unpin(k);
            if (shared.get(k)) shared.unpin(k);
        }
    };
    vector<thread> ts;
    for (int t = 0; t < 4; t++) ts.emplace_back(job, t * 100);
    for (auto& th : ts) th.join();
    cout << "  done. hits=" << shared.hits()
         << " misses=" << shared.misses() << "\n";

    return 0;
}
