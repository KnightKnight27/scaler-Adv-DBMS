#include<bits/stdc++.h> 
using namespace std ; 

template<typename Key, typename Row > 
class DB 
{ 
    private: 
        struct Entry 
        { 
            Key key ; 
            Row row ; 
        } ; 

        struct BNode 
        { 
            BNode* parent ; 
            vector<Entry > keys ; 
            vector<BNode* > rows ; 
        } ; 

        BNode* root ; 
        int minDegree ; 
        int maxDegree ; 
        int minKeys ; 
        int maxKeys ; 

        bool isFull(BNode* node ) 
        { 
            if(node->keys.size()== maxKeys ) 
            { 
                return true ; 
            } 
            return false ; 
        } 

        int searchKey(Key key, vector<Entry >& keys ) 
        { 
            int sta= 0, las= keys.size(), mid ; 
            while(sta< las ) 
            { 
                mid= ((sta+ las )/ 2 ) ; 
                if(keys[i].key< key ) 
                { 
                    sta= (mid+ 1 ) ; 
                } 
                else 
                { 
                    if(keys[(mid- 1 ) ]< key ) 
                    { 
                        return mid ; 
                    } 
                    las= (mid- 1 ) ; 
                } 
            } 
            return mid ; 
        } 

    public: 
        DB(int degree ) 
        { 
            root= new Node() ; 
            root= nullptr ; 
            minDegree= degree ; 
            maxDegree= (2* degree ) ; 
            minKeys= (minDegree- 1 ) ; 
            maxKeys= (maxDegree- 1 ) ; 
        } 

        Row search(Key key ) 
        { 
            BNode* tr= root ; 
            if(tr== nullptr ) 
            { 
                return nullptr ; 
            } 
            while(tr->rows.size()!= 0 ) 
            { 
                int siz= tr->keys.size() ; 
                for(int i= 0 ; i< siz ; ++i ) 
                { 
                    if(tr->keys[i].key== key ) 
                    { 
                        return tr->keys[i].row ; 
                    } 
                    if(key< tr->keys[i].key ) 
                    { 
                        tr= tr->rows[i] ; 
                        break ; 
                    } 
                    if(i== (siz- 1 ) ) 
                    { 
                        tr= tr->rows[(i+ 1 ) ] ; 
                    } 
                } 
            } 
            return nullptr ; 
        } 

} ; 