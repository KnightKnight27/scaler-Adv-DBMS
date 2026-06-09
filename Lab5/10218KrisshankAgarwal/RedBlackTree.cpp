#include<bits/stdc++.h> 
using namespace std ; 

struct Node 
{ 
    bool isRed ; 
    int value ; 
    Node* parent ; 
    Node* left ; 
    Node* right ; 
} ; 

class RedBlackTree 
{ 
    public: 
        Node* root ; 

        void rightRotate(Node* node ) 
        { 
            Node* gp= node->parent ; 
            Node* c= node->left ; 
            Node* cr= node->left->right ; 
            if(cr!= nullptr ) 
            { 
                cr->parent= node ; 
            } 
            node->left= cr ; 
            c->right= node ; 
            node->parent= c ; 
            if(gp!= nullptr ) 
            { 
                if(node== gp->left ) 
                { 
                    gp->left= c ; 
                } 
                else 
                { 
                    gp->right= c ; 
                } 
            } 
            else 
            { 
                root= c ; 
            } 
            c->parent= gp ; 
        } 

        void leftRotate(Node* node ) 
        { 
            Node* gp= node->parent ; 
            Node* c= node->right ; 
            Node* cl= node->right->left ; 
            if(cl!= nullptr ) 
            { 
                cl->parent= node ; 
            } 
            node->right= cl ; 
            c->left= node ; 
            node->parent= c ; 
            if(gp!= nullptr ) 
            { 
                if(node== gp->left ) 
                { 
                    gp->left= c ; 
                } 
                else 
                { 
                    gp->right= c ; 
                } 
            } 
            else 
            { 
                root= c ; 
            } 
            c->parent= gp ; 
        } 
        void insertFix(Node* node ) 
        { 
            if(node->parent== nullptr ) 
            { 
                node->isRed= false ; 
                return ; 
            } 
            else if(node->parent->isRed== false ) 
            { 
                return ; 
            } 
            else 
            { 
                Node* p= node->parent ; 
                Node* g= p->parent ; 
                if(p== g->left ) 
                { 
                    Node* u= g->right ; 
                    if(g->right!= nullptr && g->right->isRed== true ) 
                    { 
                        p->isRed= false ; 
                        u->isRed= false ; 
                        g->isRed= true ; 
                        insertFix(g ) ; 
                    } 
                    else 
                    { 
                        if(p->left== node ) 
                        { 
                            rightRotate(g ) ; 
                            p->isRed= false ; 
                            g->isRed= true ; 
                            insertFix(p ) ; 
                        } 
                        else 
                        { 
                            leftRotate(p ) ; 
                            rightRotate(g ) ; 
                            node->isRed= false ; 
                            g->isRed= true ; 
                            insertFix(node ) ; 
                        } 
                    } 
                } 
                else 
                { 
                    Node* u= g->left ; 
                    if(g->left!= nullptr && g->left->isRed== true ) 
                    { 
                        p->isRed= false ; 
                        u->isRed= false ; 
                        g->isRed= true ; 
                        insertFix(g ) ; 
                    } 
                    else 
                    { 
                        if(p->right== node ) 
                        { 
                            leftRotate(g ) ; 
                            p->isRed= false ; 
                            g->isRed= true ; 
                            insertFix(p ) ; 
                        } 
                        else 
                        { 
                            rightRotate(p ) ; 
                            leftRotate(g ) ; 
                            node->isRed= false ; 
                            g->isRed= true ; 
                            insertFix(node ) ; 
                        } 
                    } 
                } 
            } 
        } 

        Node* insertNode(int value ) 
        { 
            if(root== nullptr ) 
            { 
                Node* node= new Node() ; 
                node->isRed= false ; 
                node->value= value ; 
                node->left= nullptr ; 
                node->right= nullptr ; 
                node->parent= nullptr ; 
                root= node ; 
                return node ; 
            } 
            Node* temp= root ; 
            Node* node= new Node() ; 
            while(true ) 
            { 
                if(temp->value> value ) 
                { 
                    if(temp->left== nullptr ) 
                    { 
                        node->value= value ; 
                        node->isRed= true ; 
                        temp->left= node ; 
                        node->parent= temp ; 
                        node->left= nullptr ; 
                        node->right= nullptr ; 
                        break ; 
                    } 
                    else 
                    { 
                        temp= temp->left ; 
                    } 
                } 
                else 
                { 
                    if(temp->right== nullptr ) 
                    { 
                        node->value= value ; 
                        node->isRed= true ; 
                        temp->right= node ; 
                        node->left= nullptr ; 
                        node->right= nullptr ; 
                        node->parent= temp ; 
                        break ; 
                    } 
                    else 
                    { 
                        temp= temp->right ; 
                    } 
                } 
            } 
            if(temp->isRed== false ) 
            { 
                return node ; 
            } 
            /* cout << "Here" << endl ; */ 
            insertFix(node ) ; 
            return node ; 
        } 

        void printTree(Node* node ) 
        { 
            if(node== nullptr ) 
            { 
                return ; 
            } 
            printTree(node->left ) ; 
            cout << node->value << " " ; 
            printTree(node->right ) ; 
        } 

        Node* getRoot() 
        { 
            return root ; 
        } 
} ; 

int main() 
{ 
    RedBlackTree* tree= new RedBlackTree() ; 
    Node* nd= tree->insertNode(1 ) ; 
    tree->insertNode(2 ) ; 
    tree->insertNode(3 ) ; 
    tree->insertNode(4 ) ; 
    tree->insertNode(5 ) ; 
    tree->insertNode(6 ) ; 
    tree->insertNode(7 ) ; 
    tree->insertNode(-6 ) ; 
    tree->insertNode(8 ) ; 
    cout << nd->value << endl ; 
    Node* root= tree->getRoot() ; 
    tree->printTree(root ) ; 
    cout << endl ; 
} 