#include <bits/stdc++.h>
using namespace std;

//i am considering 1->red and 0->black[boolean hehe] :) also also one more thing that the setRB is basically fixing the tree (imma consider 4 cases here.[LL, RR,lR/RL, uncle R])
struct Node
{
    int val; bool col;
    Node *l, *r, *par;

    Node(int x){val = x, col = 1, l = r = par = nullptr;}
};

class Tree
{
    Node* root = nullptr;

    void leftrot(Node* x)
    {
        Node* tmp = x->r;
        x->r = tmp->l;
        if(tmp->l) tmp->l->par = x;
        tmp->par = x->par;

        if(!x->par) root = tmp;
        else if(x == x->par->l) x->par->l = tmp;
        else x->par->r = tmp;
        tmp->l = x;
        x->par = tmp;
    }

    void rightrot(Node* x)
    {
        Node* tmp = x->l;
        x->l = tmp->r;
        if(tmp->r) tmp->r->par = x;
        tmp->par = x->par;

        if(!x->par) root = tmp;
        else if(x == x->par->l) x->par->l = tmp;
        else x->par->r = tmp;
        tmp->r = x;
        x->par = tmp;
    }


    void setRB(Node* x)
    {
        while(x != root && x->par->col)
        {
            Node* a = x->par;
            Node* b = a->par;
    
            if(a == b->l)
            {
                Node* u = b->r;
    
                if(u && u->col)
                {
                    a->col = 0;
                    u->col = 0;
                    b->col = 1;
                    x = b;
                }
                else
                {
                    if(x == a->r)
                    {
                        leftrot(a);
                        x = a;
                        a = x->par;
                    }
    
                    rightrot(b);
    
                    a->col = 0;
                    b->col = 1;
                }
            }
            else
            {
                Node* u = b->l;
    
                if(u && u->col)
                {
                    a->col = 0;
                    u->col = 0;
                    b->col = 1;
                    x = b;
                }
                else
                {
                    if(x == a->l)
                    {
                        rightrot(a);
                        x = a;
                        a = x->par;
                    }
    
                    leftrot(b);
                    a->col = 0;
                    b->col = 1;
                }
            }
        }
    
        root->col = 0;
    }
};
//i think this completes the logical impl. , though prolly insert etc func be req too .