#include <iostream>
#include <vector>
using namespace std;

struct Node
{
    int data;
    bool red; // true = red, false = black
    Node *left, *right, *parent;

    Node(int val)
    {
        data = val;
        red = true;
        left = right = parent = nullptr;
    }
};

class RBTree
{
    Node *root;

    void rotateLeft(Node *x)
    {
        Node *y = x->right;
        x->right = y->left;
        if (y->left){
            y->left->parent = x;
        }
        y->parent = x->parent;
        if (!x->parent){
            root = y;
        }
        else if (x == x->parent->left){
            x->parent->left = y;
        }
        else {
            x->parent->right = y;
        }
        y->left = x;
        x->parent = y;
    }

    void rotateRight(Node *x)
    {
        Node *y = x->left;
        x->left = y->right;
        if (y->right){
            y->right->parent = x;
        }
        y->parent = x->parent;
        if (!x->parent){
            root = y;
        }   
        else if (x == x->parent->left) {
            x->parent->left = y;
        }
        else {
            x->parent->right = y;
        }
        y->right = x;
        x->parent = y;
    }

    void fix(Node *z)
    {
        while (z->parent && z->parent->red)
        {
            Node *par = z->parent;
            Node *grand = par->parent;

            if (par == grand->left)
            {
                Node *uncle = grand->right;
                if (uncle && uncle->red)
                { 
                    par->red = false;
                    uncle->red = false;
                    grand->red = true;
                    z = grand;
                }
                else
                {
                    if (z == par->right)
                    { 
                        rotateLeft(par);
                        z = par;
                        par = z->parent;
                    }
                    rotateRight(grand);
                    par->red = false;
                    grand->red = true;
                }
            }
            else
            {
                Node *uncle = grand->left;
                if (uncle && uncle->red)
                {
                    par->red = false;
                    uncle->red = false;
                    grand->red = true;
                    z = grand;
                }
                else
                {
                    if (z == par->left)
                    {
                        rotateRight(par);
                        z = par;
                        par = z->parent;
                    }
                    rotateLeft(grand);
                    par->red = false;
                    grand->red = true;
                }
            }
        }
        root->red = false;
    }

public:
    RBTree() { root = nullptr; }

    void put(int val)
    {
        Node *z = new Node(val);
        Node *cur = root;
        Node *par = nullptr;

        while (cur)
        { 
            par = cur;
            if (val < cur->data)
                cur = cur->left;
            else if (val > cur->data)
                cur = cur->right;
            else
            {
                delete z;
                return;
            } 
        }
        z->parent = par;
        if (!par)
            root = z;
        else if (val < par->data)
            par->left = z;
        else
            par->right = z;

        fix(z);
    }

    bool get(int val)
    {
        Node *cur = root;
        while (cur)
        {
            if (val == cur->data)
                return true;
            else if (val < cur->data)
                cur = cur->left;
            else
                cur = cur->right;
        }
        return false;
    }
};

int main()
{
    RBTree t;

    vector<int> values = {10, 20, 30, 15, 5};
    for (int val : values) {
        cout << "Inserting " << val << "\n";
        t.put(val);
    }

    cout << "GET 15: " << (t.get(15) ? "Found" : "Not Found") << "\n"; 
    cout << "GET 99: " << (t.get(99) ? "Found" : "Not Found") << "\n"; 

    return 0;
}