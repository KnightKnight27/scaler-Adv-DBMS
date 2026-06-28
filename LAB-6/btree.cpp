// Lab 6 - B-Tree (implementation)
// Bhavya Jain (23BCS10088) <Bhavya.23bcs10088@sst.scaler.com>
#include "btree.h"
#include <iostream>

using namespace std;

TreeNode::TreeNode(bool isLeaf) : leaf(isLeaf) {}

// private helpers
void BalancedTree::split_child(TreeNode *parent, int index, TreeNode *child)
{
    TreeNode *sib = new TreeNode(child->leaf);
    int mid = min_degree - 1;

    for (int i = mid + 1; i < (int)child->values.size(); ++i)
        sib->values.push_back(child->values[i]);

    if (!child->leaf)
    {
        for (int i = mid + 1; i < (int)child->children.size(); ++i)
            sib->children.push_back(child->children[i]);
    }

    int up = child->values[mid];

    child->values.resize(mid);
    if (!child->leaf)
        child->children.resize(mid + 1);

    parent->children.insert(parent->children.begin() + index + 1, sib);
    parent->values.insert(parent->values.begin() + index, up);
}

void BalancedTree::insert_nonfull(TreeNode *node, int key)
{
    if (node->leaf)
    {
        int pos = node->values.size();
        node->values.push_back(0);
        while (pos > 0 && node->values[pos - 1] > key)
        {
            node->values[pos] = node->values[pos - 1];
            --pos;
        }
        node->values[pos] = key;
    }
    else
    {
        int i = 0;
        while (i < (int)node->values.size() && key > node->values[i])
            ++i;

        if ((int)node->children[i]->values.size() == 2 * min_degree - 1)
        {
            split_child(node, i, node->children[i]);
            if (key > node->values[i])
                ++i;
        }

        insert_nonfull(node->children[i], key);
    }
}

bool BalancedTree::contains(TreeNode *node, int key)
{
    int i = 0;
    while (i < (int)node->values.size() && key > node->values[i])
        ++i;

    if (i < (int)node->values.size() && node->values[i] == key)
        return true;
    if (node->leaf)
        return false;
    return contains(node->children[i], key);
}

void BalancedTree::print_inorder(TreeNode *node)
{
    size_t i;
    for (i = 0; i < node->values.size(); ++i)
    {
        if (!node->leaf)
            print_inorder(node->children[i]);
        cout << node->values[i] << " ";
    }
    if (!node->leaf)
        print_inorder(node->children[i]);
}

void BalancedTree::collect_inorder(TreeNode *node, vector<int> &out)
{
    size_t i;
    for (i = 0; i < node->values.size(); ++i)
    {
        if (!node->leaf)
            collect_inorder(node->children[i], out);
        out.push_back(node->values[i]);
    }
    if (!node->leaf)
        collect_inorder(node->children[i], out);
}

int BalancedTree::key_index(TreeNode *node, int key)
{
    int idx = 0;
    while (idx < (int)node->values.size() && node->values[idx] < key)
        ++idx;
    return idx;
}

void BalancedTree::remove_leaf(TreeNode *node, int idx)
{
    node->values.erase(node->values.begin() + idx);
}

int BalancedTree::predecessor(TreeNode *node, int idx)
{
    TreeNode *cur = node->children[idx];
    while (!cur->leaf)
        cur = cur->children.back();
    return cur->values.back();
}

int BalancedTree::successor(TreeNode *node, int idx)
{
    TreeNode *cur = node->children[idx + 1];
    while (!cur->leaf)
        cur = cur->children.front();
    return cur->values.front();
}

void BalancedTree::merge_nodes(TreeNode *node, int idx)
{
    TreeNode *left = node->children[idx];
    TreeNode *right = node->children[idx + 1];

    left->values.push_back(node->values[idx]);
    for (int v : right->values)
        left->values.push_back(v);
    if (!left->leaf)
        for (TreeNode *c : right->children)
            left->children.push_back(c);

    node->values.erase(node->values.begin() + idx);
    node->children.erase(node->children.begin() + idx + 1);

    delete right;
}

void BalancedTree::borrow_prev(TreeNode *node, int idx)
{
    TreeNode *child = node->children[idx];
    TreeNode *left = node->children[idx - 1];

    child->values.insert(child->values.begin(), node->values[idx - 1]);
    if (!child->leaf)
    {
        child->children.insert(child->children.begin(), left->children.back());
        left->children.pop_back();
    }

    node->values[idx - 1] = left->values.back();
    left->values.pop_back();
}

void BalancedTree::borrow_next(TreeNode *node, int idx)
{
    TreeNode *child = node->children[idx];
    TreeNode *right = node->children[idx + 1];

    child->values.push_back(node->values[idx]);
    if (!child->leaf)
    {
        child->children.push_back(right->children.front());
        right->children.erase(right->children.begin());
    }

    node->values[idx] = right->values.front();
    right->values.erase(right->values.begin());
}

void BalancedTree::fill_child(TreeNode *node, int idx)
{
    if (idx != 0 && (int)node->children[idx - 1]->values.size() >= min_degree)
        borrow_prev(node, idx);
    else if (idx != (int)node->values.size() && (int)node->children[idx + 1]->values.size() >= min_degree)
        borrow_next(node, idx);
    else
    {
        if (idx != (int)node->values.size())
            merge_nodes(node, idx);
        else
            merge_nodes(node, idx - 1);
    }
}

void BalancedTree::remove_internal(TreeNode *node, int idx)
{
    int key = node->values[idx];

    if ((int)node->children[idx]->values.size() >= min_degree)
    {
        int pred = predecessor(node, idx);
        node->values[idx] = pred;
        remove_node(node->children[idx], pred);
    }
    else if ((int)node->children[idx + 1]->values.size() >= min_degree)
    {
        int succ = successor(node, idx);
        node->values[idx] = succ;
        remove_node(node->children[idx + 1], succ);
    }
    else
    {
        merge_nodes(node, idx);
        remove_node(node->children[idx], key);
    }
}

void BalancedTree::remove_node(TreeNode *node, int key)
{
    int idx = key_index(node, key);

    if (idx < (int)node->values.size() && node->values[idx] == key)
    {
        if (node->leaf)
            remove_leaf(node, idx);
        else
            remove_internal(node, idx);
    }
    else
    {
        if (node->leaf)
            return;

        bool last = (idx == (int)node->values.size());
        if ((int)node->children[idx]->values.size() < min_degree)
            fill_child(node, idx);

        if (last && idx > (int)node->values.size())
            remove_node(node->children[idx - 1], key);
        else
            remove_node(node->children[idx], key);
    }
}

void BalancedTree::free_node(TreeNode *node)
{
    if (!node)
        return;
    if (!node->leaf)
        for (TreeNode *c : node->children)
            free_node(c);
    delete node;
}

// public
BalancedTree::BalancedTree(int t) : rootNode(new TreeNode(true)), min_degree(t) {}
BalancedTree::~BalancedTree() { free_node(rootNode); }

void BalancedTree::insert(int key)
{
    if ((int)rootNode->values.size() == 2 * min_degree - 1)
    {
        TreeNode *newRoot = new TreeNode(false);
        newRoot->children.push_back(rootNode);
        split_child(newRoot, 0, rootNode);
        rootNode = newRoot;
    }
    insert_nonfull(rootNode, key);
}

void BalancedTree::remove(int key)
{
    if (rootNode->values.empty())
        return;
    remove_node(rootNode, key);

    if (rootNode->values.empty())
    {
        TreeNode *old = rootNode;
        if (rootNode->leaf)
            rootNode = new TreeNode(true);
        else
            rootNode = rootNode->children[0];
        delete old;
    }
}

bool BalancedTree::search(int key) { return contains(rootNode, key); }

void BalancedTree::print_inorder()
{
    print_inorder(rootNode);
    cout << endl;
}

void BalancedTree::print_levels()
{
    if (rootNode->values.empty())
    {
        cout << "(empty tree)\n";
        return;
    }

    queue<pair<TreeNode *, int>> q;
    q.push({rootNode, 0});
    int curLevel = -1;

    while (!q.empty())
    {
        auto pr = q.front();
        q.pop();
        TreeNode *n = pr.first;
        int lvl = pr.second;
        if (lvl != curLevel)
        {
            if (curLevel != -1)
                cout << '\n';
            cout << "L" << lvl << ":  ";
            curLevel = lvl;
        }

        cout << "[";
        for (size_t i = 0; i < n->values.size(); ++i)
        {
            cout << n->values[i];
            if (i + 1 < n->values.size())
                cout << ", ";
        }
        cout << "]  ";

        if (!n->leaf)
            for (TreeNode *c : n->children)
                q.push({c, lvl + 1});
    }
    cout << '\n';
}

void BalancedTree::collect_inorder(vector<int> &out) { collect_inorder(rootNode, out); }
