#include <iostream>
#include <vector>
#include <string>

using namespace std;

template <class K, class V>
class BTreeDatabase {
private:
    struct Record {
        K id;
        V data;
    };

    struct TreeNode {
        bool leafNode;
        vector<Record> records;
        vector<TreeNode*> childPtrs;

        TreeNode(bool leaf) : leafNode(leaf) {}
    };

    int degree;
    TreeNode* treeRoot;

    void divideChild(TreeNode* parentNode, int childIndex) {
        TreeNode* currentChild = parentNode->childPtrs[childIndex];
        TreeNode* rightHalf = new TreeNode(currentChild->leafNode);

        int t = degree;

        for (int i = 0; i < t - 1; i++)
            rightHalf->records.push_back(currentChild->records[i + t]);

        if (!currentChild->leafNode) {
            for (int i = 0; i < t; i++)
                rightHalf->childPtrs.push_back(currentChild->childPtrs[i + t]);
        }

        Record middleRecord = currentChild->records[t - 1];

        currentChild->records.resize(t - 1);

        if (!currentChild->leafNode)
            currentChild->childPtrs.resize(t);

        parentNode->childPtrs.insert(
            parentNode->childPtrs.begin() + childIndex + 1,
            rightHalf
        );

        parentNode->records.insert(
            parentNode->records.begin() + childIndex,
            middleRecord
        );
    }

    void insertIntoNode(TreeNode* node, K key, V value) {
        int pos = node->records.size() - 1;

        if (node->leafNode) {
            node->records.push_back({key, value});

            while (pos >= 0 && node->records[pos].id > key) {
                node->records[pos + 1] = node->records[pos];
                pos--;
            }

            node->records[pos + 1] = {key, value};
        }
        else {
            while (pos >= 0 && node->records[pos].id > key)
                pos--;

            pos++;

            if (node->childPtrs[pos]->records.size() == 2 * degree - 1) {
                divideChild(node, pos);

                if (key > node->records[pos].id)
                    pos++;
            }

            insertIntoNode(node->childPtrs[pos], key, value);
        }
    }

    void inorderTraversal(TreeNode* node) {
        if (node == nullptr)
            return;

        int n = node->records.size();

        for (int i = 0; i < n; i++) {
            if (!node->leafNode)
                inorderTraversal(node->childPtrs[i]);

            cout << "("
                 << node->records[i].id
                 << " -> "
                 << node->records[i].data
                 << ") ";
        }

        if (!node->leafNode)
            inorderTraversal(node->childPtrs[n]);
    }

public:
    BTreeDatabase(int minDeg) {
        degree = minDeg;
        treeRoot = new TreeNode(true);
    }

    void addRecord(K key, V value) {
        if (treeRoot->records.size() == 2 * degree - 1) {
            TreeNode* freshRoot = new TreeNode(false);

            freshRoot->childPtrs.push_back(treeRoot);
            treeRoot = freshRoot;

            divideChild(treeRoot, 0);
        }

        insertIntoNode(treeRoot, key, value);
    }

    bool findRecord(K key, V& output) {
        TreeNode* current = treeRoot;

        while (current != nullptr) {
            int idx = 0;

            while (idx < current->records.size() &&
                   key > current->records[idx].id)
                idx++;

            if (idx < current->records.size() &&
                current->records[idx].id == key) {
                output = current->records[idx].data;
                return true;
            }

            if (current->leafNode)
                break;

            current = current->childPtrs[idx];
        }

        return false;
    }

    void printTree() {
        inorderTraversal(treeRoot);
        cout << endl;
    }
};

int main() {
    BTreeDatabase<int, string> studentDB(3);

    studentDB.addRecord(10, "Alice");
    studentDB.addRecord(20, "Bob");
    studentDB.addRecord(5, "Charlie");
    studentDB.addRecord(15, "Diana");
    studentDB.addRecord(25, "Eve");
    studentDB.addRecord(30, "Frank");
    studentDB.addRecord(1, "Grace");

    cout << "Stored Records:\n";
    studentDB.printTree();

    string result;

    int searchKey = 20;
    if (studentDB.findRecord(searchKey, result))
        cout << "Record Found: " << result << endl;
    else
        cout << "Record Not Found\n";

    searchKey = 99;
    if (studentDB.findRecord(searchKey, result))
        cout << "Record Found: " << result << endl;
    else
        cout << "Record Not Found\n";

    return 0;
}