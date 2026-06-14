#include <iostream>
#include <string>

using namespace std;

enum Color
{
    RED,
    BLACK
};

struct Node
{
    int data;
    Color color;
    Node *left, *right, *parent;

    Node(int data) : data(data), color(RED), left(nullptr), right(nullptr), parent(nullptr) {}
};

class RedBlackTree
{
private:
    Node *root;
    Node *TNULL;

    void preOrderHelper(Node *node)
    {
        if (node != TNULL)
        {
            cout << node->data << " ";
            preOrderHelper(node->left);
            preOrderHelper(node->right);
        }
    }

    void inOrderHelper(Node *node)
    {
        if (node != TNULL)
        {
            inOrderHelper(node->left);
            cout << node->data << "(" << (node->color == RED ? "R" : "B") << ") ";
            inOrderHelper(node->right);
        }
    }

    void postOrderHelper(Node *node)
    {
        if (node != TNULL)
        {
            postOrderHelper(node->left);
            postOrderHelper(node->right);
            cout << node->data << " ";
        }
    }

    Node *searchTreeHelper(Node *node, int key)
    {
        if (node == TNULL || key == node->data)
        {
            return node;
        }

        if (key < node->data)
        {
            return searchTreeHelper(node->left, key);
        }
        return searchTreeHelper(node->right, key);
    }

    void fixInsert(Node *k)
    {
        Node *u;
        while (k->parent->color == RED)
        {
            if (k->parent == k->parent->parent->right)
            {
                u = k->parent->parent->left; // uncle
                if (u->color == RED)
                {
                    // case 3.1
                    u->color = BLACK;
                    k->parent->color = BLACK;
                    k->parent->parent->color = RED;
                    k = k->parent->parent;
                }
                else
                {
                    if (k == k->parent->left)
                    {
                        // case 3.2.2
                        k = k->parent;
                        rightRotate(k);
                    }
                    // case 3.2.1
                    k->parent->color = BLACK;
                    k->parent->parent->color = RED;
                    leftRotate(k->parent->parent);
                }
            }
            else
            {
                u = k->parent->parent->right; // uncle

                if (u->color == RED)
                {
                    // mirror case 3.1
                    u->color = BLACK;
                    k->parent->color = BLACK;
                    k->parent->parent->color = RED;
                    k = k->parent->parent;
                }
                else
                {
                    if (k == k->parent->right)
                    {
                        // mirror case 3.2.2
                        k = k->parent;
                        leftRotate(k);
                    }
                    // mirror case 3.2.1
                    k->parent->color = BLACK;
                    k->parent->parent->color = RED;
                    rightRotate(k->parent->parent);
                }
            }
            if (k == root)
            {
                break;
            }
        }
        root->color = BLACK;
    }

    void printHelper(Node *root, string indent, bool last)
    {
        if (root != TNULL)
        {
            cout << indent;
            if (last)
            {
                cout << "R----";
                indent += "     ";
            }
            else
            {
                cout << "L----";
                indent += "|    ";
            }
            string sColor = root->color == RED ? "RED" : "BLACK";
            cout << root->data << "(" << sColor << ")" << endl;
            printHelper(root->left, indent, false);
            printHelper(root->right, indent, true);
        }
    }

    int checkBlackHeight(Node *node)
    {
        if (node == TNULL)
            return 1;

        int leftBH = checkBlackHeight(node->left);
        int rightBH = checkBlackHeight(node->right);

        if (leftBH == -1 || rightBH == -1 || leftBH != rightBH)
        {
            return -1; // violation
        }

        if (node->color == RED)
        {
            if (node->left->color == RED || node->right->color == RED)
            {
                return -1; // Red node has red child violation
            }
            return leftBH;
        }
        else
        {
            return leftBH + 1;
        }
    }

public:
    RedBlackTree()
    {
        TNULL = new Node(0);
        TNULL->color = BLACK;
        TNULL->left = nullptr;
        TNULL->right = nullptr;
        root = TNULL;
    }

    void preorder()
    {
        preOrderHelper(this->root);
        cout << endl;
    }

    void inorder()
    {
        inOrderHelper(this->root);
        cout << endl;
    }

    void postorder()
    {
        postOrderHelper(this->root);
        cout << endl;
    }

    Node *searchTree(int k)
    {
        Node *res = searchTreeHelper(this->root, k);
        if (res == TNULL)
        {
            return nullptr;
        }
        return res;
    }

    void leftRotate(Node *x)
    {
        Node *y = x->right;
        x->right = y->left;
        if (y->left != TNULL)
        {
            y->left->parent = x;
        }
        y->parent = x->parent;
        if (x->parent == nullptr)
        {
            this->root = y;
        }
        else if (x == x->parent->left)
        {
            x->parent->left = y;
        }
        else
        {
            x->parent->right = y;
        }
        y->left = x;
        x->parent = y;
    }

    void rightRotate(Node *x)
    {
        Node *y = x->left;
        x->left = y->right;
        if (y->right != TNULL)
        {
            y->right->parent = x;
        }
        y->parent = x->parent;
        if (x->parent == nullptr)
        {
            this->root = y;
        }
        else if (x == x->parent->right)
        {
            x->parent->right = y;
        }
        else
        {
            x->parent->left = y;
        }
        y->right = x;
        x->parent = y;
    }

    void insert(int key)
    {
        Node *node = new Node(key);
        node->parent = nullptr;
        node->data = key;
        node->left = TNULL;
        node->right = TNULL;
        node->color = RED;

        Node *y = nullptr;
        Node *x = this->root;

        while (x != TNULL)
        {
            y = x;
            if (node->data < x->data)
            {
                x = x->left;
            }
            else
            {
                x = x->right;
            }
        }

        node->parent = y;
        if (y == nullptr)
        {
            root = node;
        }
        else if (node->data < y->data)
        {
            y->left = node;
        }
        else
        {
            y->right = node;
        }

        if (node->parent == nullptr)
        {
            node->color = BLACK;
            return;
        }

        if (node->parent->parent == nullptr)
        {
            return;
        }

        fixInsert(node);
    }

    void printTree()
    {
        if (root)
        {
            printHelper(this->root, "", true);
        }
    }

    bool verifyProperties()
    {
        if (root == TNULL)
            return true;

        if (root->color != BLACK)
        {
            cout << "Property violation: Root is not BLACK." << endl;
            return false;
        }
        int bh = checkBlackHeight(root);
        if (bh == -1)
        {
            cout << "Property violation: Black height mismatch or Red node has Red child." << endl;
            return false;
        }
        return true;
    }
};

int main()
{
    RedBlackTree bst;

    int values[] = {55, 40, 65, 60, 75, 57};
    for (int val : values)
    {
        cout << "Inserting " << val << "..." << endl;
        bst.insert(val);
        bst.printTree();
        cout << endl;
    }

    int searchKeys[] = {60, 100};
    for (int key : searchKeys)
    {
        cout << "Searching for " << key << "... ";
        Node *res = bst.searchTree(key);
        if (res != nullptr && res->data == key)
        {
            cout << "Found! " << "(Color: " << (res->color == RED ? "RED" : "BLACK") << ")" << endl;
        }
        else
        {
            cout << "Not Found!" << endl;
        }
    }

    cout << "\nInorder representation: ";
    bst.inorder();

    if (bst.verifyProperties())
    {
        cout << "Tree properties are maintained successfully!" << endl;
    }
    else
    {
        cout << "Tree property violation found!" << endl;
    }

    return 0;
}