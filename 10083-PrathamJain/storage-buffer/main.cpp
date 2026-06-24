#include <bits/stdc++.h>
#define mii map<int,int>
#define endl "\n"
using namespace std;


struct Node
{
    int id, bit, bad, val;
    Node()
    {
        id = -1, bit = 0, bad = 0, val = 0;
    }
};



class ClockSweep
{
    vector<Node> vec;
    mii mp;
    int cap, clock;

public:
    ClockSweep(int sz)
    {
        cap = sz, clock = 0; 
        vec.resize(cap);
    }

    void read(int id)
    {
        if(mp.find(id) != mp.end())
        {
            auto it = mp[id];
            vec[it].bit = 1;
            return;
        }
        load(id, 0);
    }

    void write(int id)
    {
        if(mp.find(id) != mp.end())
        {
            auto it = mp[id];
            vec[it].bit = 1;
            vec[it].bad = 0;
            return;
        }
        
        load(id, 1);
    }

    void load(int id, bool check)
    {

        auto it = find();

        if(it == -1)
        {
            cout <<"Not available"<<endl;
            return;
        }

        if(vec[it].val)
        {
            int old = vec[it].id;
            mp.erase(old);
        }

        vec[it].id = id;
        vec[it].bit = 1;
        vec[it].bad = check;
        vec[it].val = 1;
        mp[id] = it;

    }


    int find()
    {
        int cnt = 0;
        while(true)
        {
            if(!vec[clock].val)
            {
                int x = clock;
                clock = (clock+1)%cap;
                return x;
            }

            if(vec[clock].bit == 0)
            {
                int x = clock;
                clock = (clock+1)%cap;
                return x;
            }
            vec[clock].bit = 0;
            clock = (clock+1)%cap;
            cnt++;
        }
        return -1;
    }
};