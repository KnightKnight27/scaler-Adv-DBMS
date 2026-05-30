#include <bits/stdc++.h>
using namespace std;

#define endl "\n"
#define all(x) x.begin(), x.end()
const int M = 3; 

struct Node {
    bool leaf;

    vector<int> keys;
    vector<Node*> child;

    Node* next;

    Node(bool l = true) 
    {
        leaf = l, next = nullptr;
    }
};

class BPlusTree 
{
    Node* root = new Node(true);

    void splitChild(Node* par, int idx) 
    {
        Node* cur = par->child[idx];
        Node* newNode = new Node(cur->leaf);
        int mid = M / 2;
        if(cur->leaf) 
        {

            for(int i = mid; i < cur->keys.size(); i++) newNode->keys.push_back(cur->keys[i]);

            cur->keys.resize(mid);

            newNode->next = cur->next;
            cur->next = newNode;

            par->keys.insert(par->keys.begin() + idx, newNode->keys[0]);
        }
        else 
        {

            int promote = cur->keys[mid];
            for(int i = mid + 1; i < cur->keys.size(); i++) newNode->keys.push_back(cur->keys[i]);
            for(int i = mid + 1; i < cur->child.size(); i++) newNode->child.push_back(cur->child[i]);

            cur->keys.resize(mid);
            cur->child.resize(mid + 1);

            par->keys.insert(par->keys.begin() + idx,promote);
        }

        par->child.insert(par->child.begin() + idx + 1,newNode);
    }

    void insertNonFull(Node* cur, int key) 
    {
        if(cur->leaf) 
        {
            cur->keys.insert(lower_bound(all(cur->keys),key),key);
        }
        else 
        {
            int idx = upper_bound(all(cur->keys),key) - cur->keys.begin();
            if(cur->child[idx]->keys.size() == M) 
            {
                splitChild(cur, idx);
                if(key >= cur->keys[idx])
                    idx++;
            }
            insertNonFull(cur->child[idx], key);
        }
    }

public:

    void insert(int key) 
    {
        if(root->keys.size() == M) 
        {
            Node* newRoot = new Node(false);
            newRoot->child.push_back(root);
            splitChild(newRoot, 0);
            root = newRoot;
        }
        insertNonFull(root, key);
    }

};