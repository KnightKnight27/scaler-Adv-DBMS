#include<bits/stdc++.h> 
using namespace std ; 

bool isOperator(string ch ) 
{ 
    if(ch== "+" || ch== "*" || ch== "/" || ch== "-" || ch== "(" || ch== ")" ) 
    { 
        return true ; 
    } 
    return false ; 
} 

int assignPriority(char ch ) 
{ 
    if(ch== '+' || ch== '-' ) 
    { 
        return 0 ; 
    } 
    else if(ch== '*' || ch== '/' ) 
    { 
        return 1 ; 
    } 
    else 
    { 
        return 2 ; 
    } 
} 

int performOperation(int no, int nt, char ope ) 
{ 
    if(ope== '+' ) 
    { 
        return (no+ nt ) ; 
    } 
    else if(ope== '-' ) 
    { 
        return (no- nt ) ; 
    } 
    else if(ope== '/' ) 
    { 
        return (no/ nt ) ; 
    } 
    else 
    { 
        return (no* nt ) ; 
    } 
} 

int checkPriority(char c, char ch ) 
{ 
    return (assignPriority(c )- assignPriority(ch ) ) ; 
} 
int main() 
{ 
    string s ; 
    /* getline(cin, s ) ; */ 
    s= "3 + 2 * 5 - 7 + ( 8 + 4 / 2 + 1 + ( 2 / 1 + ( 5 - 2 ) ) )" ; 
    /* cout << s << endl ; */ 
    s= s+ ' ' ; 
    vector<string > units ; 
    int len= s.length() ; 
    /* cout << len << endl ; */ 
    int c= 0 ; 
    string wrd= "" ; 
    for(int i= 0 ; i< len ; ++i ) 
    { 
        if(s[i]!= ' ' ) 
        { 
            c++ ; 
        } 
        if(c> 0 ) 
        { 
            if(s[i]!= ' ' ) 
            { 
                wrd= wrd+ s[i] ; 
            } 
            else if((s[i]== ' ' ) && (wrd.length()> 0 ) ) 
            { 
                units.push_back(wrd ) ; 
                wrd= "" ; 
            } 
        } 
    } 
    int ln= units.size() ; 
    vector<int > num ; 
    vector<char > operators ; 
    for(int i= 0 ; i< ln ; ++i ) 
    { 
        if(isOperator(units[i] ) ) 
        { 
            if(operators.size()== 0 ) 
            { 
                operators.push_back(units[i][0] ) ; 
            } 
            else 
            { 
                int pri= checkPriority(operators.back(), units[i][0] ) ; 
                if(operators.back()== '(' || operators.back()== ')' ) 
                { 
                    operators.push_back(units[i][0] ) ; 
                    continue ; 
                } 
                if(pri>= 0 ) 
                { 
                    int no= num.back() ; 
                    num.pop_back() ; 
                    int nt= num.back() ; 
                    num.pop_back() ; 
                    int res= performOperation(nt, no, operators.back() ) ; 
                    operators.pop_back() ; 
                    operators.push_back(units[i][0] ) ; 
                    num.push_back(res ) ; 
                    /* cout << res << " " ; */ 
                } 
                else 
                { 
                    operators.push_back(units[i][0] ) ; 
                } 
                if(units[i][0]== ')' ) 
                { 
                    operators.pop_back() ; 
                    while(operators.back()!= '(' ) 
                    { 
                        int no= num.back() ; 
                        num.pop_back() ; 
                        int nt= num.back() ; 
                        num.pop_back() ; 
                        int res= performOperation(nt, no, operators.back() ) ; 
                        operators.pop_back() ; 
                        num.push_back(res ) ; 
                    } 
                    operators.pop_back() ; 
                } 
            } 
        } 
        else 
        { 
            num.push_back(stoi(units[i] ) ) ; 
        } 
    } 
    int sz= num.size() ; 
    for(int i= 0 ; i< (sz- 1 ) ; ++i ) 
    { 
        int nf= num.back() ; 
        num.pop_back() ; 
        int np= num.back() ; 
        num.pop_back() ; 
        int res= performOperation(np, nf, operators.back() ) ; 
        operators.pop_back() ; 
        num.push_back(res ) ; 
    } 
    cout << num[0] << endl ; 
} 