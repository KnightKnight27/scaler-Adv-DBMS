#include <iostream>
#include <unordered_map>
#include <vector>
#include <set>
#include <queue>
using namespace std;

struct Version {
    int value;
    int txnId;

    Version(int v, int t) {
        value = v;
        txnId = t;
    }
};

class MVCCStorage {
    unordered_map<string, vector<Version>> data;

public:
    void write(string key, int value, int txnId) {
        data[key].push_back(Version(value, txnId));
    }

    int read(string key) {
        if(data[key].empty())
            return -1;
        return data[key].back().value;
    }

    void printVersions(string key) {
        cout<<"Versions for "<<key<<": ";
        for(auto &v : data[key])
            cout<<"["<<v.value<<", T"<<v.txnId<<"] ";
        cout<<endl;
    }
};

class LockManager {
    unordered_map<string, int> lockOwner;
    unordered_map<int, set<int>> waitGraph;

public:
    bool acquireLock(int txnId, string item) {

        if(!lockOwner.count(item)) {
            lockOwner[item] = txnId;
            cout<<"T"<<txnId<<" got lock on "<<item<<endl;
            return true;
        }

        if(lockOwner[item]==txnId)
            return true;

        int owner = lockOwner[item];
        waitGraph[txnId].insert(owner);
        cout<<"T"<<txnId<<" waiting for T"<<owner<<endl;
        return false;
    }

    void releaseLock(int txnId, string item) {
        if(lockOwner.count(item) && lockOwner[item]==txnId) {
            lockOwner.erase(item);
            cout<<"T"<<txnId<<" released "<<item<<endl;
        }
    }

    bool detectDeadlock() {
        unordered_map<int, int> indegree;

        for(auto &p : waitGraph) {
            if(!indegree.count(p.first))
                indegree[p.first]=0;
            for(int v : p.second)
                indegree[v]++;
        }

        queue<int> q;
        for(auto &p : indegree)
            if(p.second==0)
                q.push(p.first);

        int visited=0;
        while(!q.empty()) {
            int u=q.front();
            q.pop();
            visited++;
            for(int v : waitGraph[u]) {
                indegree[v]--;
                if(indegree[v]==0)
                    q.push(v);
            }
        }

        return visited != indegree.size();
    }
};

class TransactionManager {
    int nextTxn = 1;

public:
    int begin() {
        cout<<"T"<<nextTxn<<" started"<<endl;
        return nextTxn++;
    }

    void commit(int txnId) {
        cout<<"T"<<txnId<<" committed"<<endl;
    }

    void abort(int txnId) {
        cout<<"T"<<txnId<<" aborted"<<endl;
    }
};

int main() {

    TransactionManager tm;
    LockManager lm;
    MVCCStorage db;

    int T1 = tm.begin();
    int T2 = tm.begin();

    lm.acquireLock(T1, "A");
    db.write("A", 100, T1);

    lm.acquireLock(T2, "B");
    db.write("B", 200, T2);

    lm.acquireLock(T1, "B");
    lm.acquireLock(T2, "A");

    if(lm.detectDeadlock()) {
        cout<<"\nDeadlock Detected!\n";
        tm.abort(T2);
    }

    db.printVersions("A");
    db.printVersions("B");

    tm.commit(T1);

    return 0;
}
