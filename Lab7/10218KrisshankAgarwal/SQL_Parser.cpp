#include<bits/stdc++.h> 
using namespace std ; 

struct Row 
{ 
    long long id ; 
    string name ; 
    int age ; 
} ; 
struct studentTable 
{ 
    vector<Row* > rows ; 
} ; 

class Parser 
{ 
    public: 
        void parseQuery(string sqlQuery ) 
        { 
            sqlQuery= sqlQuery.substr(6 ) ; 
            int c= 0 ; 
            int ln= sqlQuery.length() ; 
            string operation= "SELECT", wrd= "" ; 
            for(int i= 0 ; i< ln ; ++i ) 
            { 
                if(sqlQuery[i]!= ' ' ) 
                { 
                    c++ ; 
                } 
                if(c> 0 ) 
                { 
                    if(sqlQuery[i]== ' ' && wrd.length()> 0 ) 
                    { 
                        if(operation== "SELECT" && wrd!= "," ) 
                        { 
                            columns.push_back(wrd ) ; 
                        } 
                        else if(operation== "FROM" ) ; 
                        { 
                            table= wrd ; 
                        } 
                        wrd= "" ; 
                    } 
                    else 
                    { 
                        wrd= wrd+ sqlQuery[i] ; 
                    } 
                    if(wrd== "FROM" ) 
                    { 
                        operation= "FROM" ; 
                    } 
                    else if(wrd== "WHERE" ) 
                    { 
                        filter= sqlQuery.substr((i+ 2 ), sqlQuery.find(';' ) ) ; 
                        break ; 
                    } 
                } 
            } 
        } 

        string getResultFromTable(studentTable* st ) 
        { 
            vector<Row* > rows= getRowsFromTable(st ) ; 
            int sz= rows.size() ; 
            int szc= columns.size() ; 
            string studentResult= "" ; 
            for(int i= 0 ; i< szc ; ++i ) 
            { 
                cout << columns[i] << " " ; 
            } 
            cout << endl ; 
            for(int i= 0 ; i< sz ; ++i ) 
            { 
                string studentRow= "" ; 
                for(int j= 0 ; j< szc ; ++j ) 
                { 
                    if(columns[j]== "age" ) 
                    { 
                        studentRow= (studentRow+ to_string(rows[i]->age )+ " " ) ; 
                    } 
                    else if(columns[j]== "id" ) 
                    { 
                        studentRow= (studentRow+ to_string(rows[i]->id )+ " " ) ; 
                    } 
                    else if(columns[j]== "name" ) 
                    { 
                        studentRow= (studentRow+ rows[i]->name )+ " " ; 
                    } 
                } 
                studentRow= (studentRow+ '\n' ) ; 
                studentResult= (studentResult+ studentRow ) ; 
            } 
            return studentResult ; 
        } 

    private: 
        vector<string > columns ; 
        string table ; 
        string filter ; 

        bool evaluateExpression(Row* row, string oa, string ob, string op ) 
        { 
            int ln= columns.size() ; 
            string col= "" ; 
            string cm= "" ; 
            int c= 0 ; 
            for(int i= 0 ; i< ln ; ++i ) 
            { 
                if(columns[i]== oa ) 
                { 
                    col= oa ; 
                    if(ob[0]== '"' ) 
                    { 
                        cm= ob.substr(1, (ob.length()- 2 ) ) ; 
                        /* cout << ob.length() << " " << cm << endl ; */ 
                    } 
                    else 
                    { 
                        cm= ob ; 
                    } 
                    if(op== ">=" ) 
                    { 
                        if(col== "id" ) 
                        { 
                            return row->id>= stoi(cm ) ; 
                        } 
                        else if(col== "age" ) 
                        { 
                            return row->age>= stoi(cm ) ; 
                        } 
                        else 
                        { 
                            return row->name>= cm ; 
                        } 
                    } 
                    else if(op== "<=" ) 
                    { 
                        if(col== "id" ) 
                        { 
                            return row->id<= stoi(cm ) ; 
                        } 
                        else if(col== "age" ) 
                        { 
                            return row->age<= stoi(cm ) ; 
                        } 
                        else 
                        { 
                            return row->name<= cm ; 
                        } 
                    } 
                    else if(op== ">" ) 
                    { 
                        if(col== "id" ) 
                        { 
                            return row->id> stoi(cm ) ; 
                        } 
                        else if(col== "age" ) 
                        { 
                            return row->age> stoi(cm ) ; 
                        } 
                        else 
                        { 
                            return row->name> cm ; 
                        } 
                    } 
                    else if(op== "<" ) 
                    { 
                        if(col== "id" ) 
                        { 
                            return row->id< stoi(cm ) ; 
                        } 
                        else if(col== "age" ) 
                        { 
                            return row->age< stoi(cm ) ; 
                        } 
                        else 
                        { 
                            return row->name< cm ; 
                        } 
                    } 
                    c++ ; 
                    break ; 
                } 
            } 
            if(c== 0 ) 
            { 
                col= ob ; 
                if(oa[0]== '"' ) 
                { 
                    cm= oa.substr(1, (oa.length()- 2 ) ) ; 
                } 
                else 
                { 
                    cm= oa ; 
                } 
                if(op== ">=" ) 
                { 
                    if(col== "name" ) 
                    { 
                        return cm>= row->name ; 
                    } 
                    else if(col== "age" ) 
                    { 
                        return stoi(cm )>= row->age ; 
                    } 
                    else 
                    { 
                        return stoi(cm )>= row->id ; 
                    } 
                } 
                else if(op== "<=" ) 
                { 
                    if(col== "name" ) 
                    { 
                        return cm<= row->name ; 
                    } 
                    else if(col== "age" ) 
                    { 
                        return stoi(cm )<= row->age ; 
                    } 
                    else 
                    { 
                        return stoi(cm )<= row->id ; 
                    } 
                } 
                else if(op== ">" ) 
                { 
                    if(col== "name" ) 
                    { 
                        return cm> row->name ; 
                    } 
                    else if(col== "id" ) 
                    { 
                        return stoi(cm )> row->id ; 
                    } 
                    else 
                    { 
                        return stoi(cm )> row->age ; 
                    } 
                } 
                else if(op== "<" ) 
                { 
                    if(col== "name" ) 
                    { 
                        return cm< row->name ; 
                    } 
                    else if(col== "age" ) 
                    { 
                        return (stoi(cm ) )< row->age ; 
                    } 
                    else 
                    { 
                        return (stoi(cm ) )< row->id ; 
                    } 
                } 
            } 
            if(op== "=" ) 
            { 
                if(col== "name" ) 
                { 
                    return row->name== cm ; 
                } 
                else if(col== "age" ) 
                { 
                    return row->age== stoi(cm ) ; 
                } 
                else 
                { 
                    return row->id== stoi(cm ) ; 
                } 
            } 
            else 
            { 
                if(col== "name" ) 
                { 
                    return row->name!= cm ; 
                } 
                else if(col== "age" ) 
                { 
                    return row->age!= stoi(cm ) ; 
                } 
                else 
                { 
                    return row->id!= stoi(cm ) ; 
                } 
            } 
        } 

        bool isValid(Row* row ) 
        { 
            string opb= "" ; 
            string op= "" ; 
            string wrd= "" ; 
            int ln= filter.length() ; 
            vector<bool > vec ; 
            vector<string > condition ; 
            for(int i= 0 ; i< ln ; ++i ) 
            { 
                if(filter[i]!= ' ' ) 
                { 
                    wrd= wrd+ filter[i] ; 
                } 
                else 
                { 
                    if(op== "<" || op== ">" || op== "=" || op== ">=" || op== "<=" || op== "!=" ) 
                    { 
                        bool eve= evaluateExpression(row, opb, wrd, op ) ; 
                        /* if(row->name== "Samrat" ) 
                        { 
                            cout << eve << " " ; 
                        } */ 
                        vec.push_back(eve ) ; 
                    } 
                    if(wrd== "AND" || wrd== "OR" || wrd== "(" ) 
                    { 
                        if((wrd!= "(" ) && (condition.back()== "AND" || condition.back()== "OR" ) ) 
                        { 
                            bool exb= vec.back() ; 
                            vec.pop_back() ; 
                            bool exa= vec.back() ; 
                            vec.pop_back() ; 
                            bool res= "" ; 
                            if(condition.back()== "AND" ) 
                            { 
                                res= (exb && exa ) ; 
                            } 
                            else 
                            { 
                                res= (exb || exa ) ; 
                            } 
                            condition.pop_back() ; 
                            vec.push_back(res ) ; 
                        } 
                        condition.push_back(wrd ) ; 
                    } 
                    if(wrd== ")" ) 
                    { 
                        while(condition.back()!= "(" ) 
                        { 
                            bool exb= vec.back() ; 
                            vec.pop_back() ; 
                            bool exa= vec.back() ; 
                            vec.pop_back() ; 
                            bool res= "" ; 
                            if(condition.back()== "AND" ) 
                            { 
                                res= (exa && exb ) ; 
                            } 
                            else 
                            { 
                                res= (exa || exb ) ; 
                            } 
                            condition.pop_back() ; 
                            vec.push_back(res ) ; 
                        } 
                        condition.pop_back() ; 
                    } 
                    opb= op ; 
                    op= wrd ; 
                    wrd= "" ; 
                } 
            } 
            int len= condition.size() ; 
            bool exs= vec[0] ; 
            for(int i= 0 ; i< len ; ++i ) 
            { 
                if(condition[i]== "AND" ) 
                { 
                    exs= (exs && vec[(i+ 1 ) ] ) ; 
                } 
                else 
                { 
                    exs= (exs || vec[(i+ 1 ) ] ) ; 
                } 
            } 
            return exs ; 
        } 

        vector<Row* > getRowsFromTable(studentTable* st ) 
        { 
            studentTable* stc ; 
            if(table== "student_table" ) 
            { 
                stc= st ; 
            } 
            int sz= stc->rows.size() ; 
            vector<Row* > rwc ; 
            for(int i= 0 ; i< sz ; ++i ) 
            { 
                if(isValid(stc->rows[i] ) ) 
                { 
                    rwc.push_back(stc->rows[i] ) ; 
                } 
            } 
            return rwc ; 
        } 
} ; 
int main() 
{ 
    string query= "SELECT id , name , age FROM student_table WHERE ( \"Samrat\" = name ) AND ( age > 2 OR ( age < 28 ) )  ; " ; 
    studentTable* st= new studentTable() ; 
    Row* rwa= new Row() ; 
    rwa->id= 1 ; 
    rwa->name= "Tushar" ; 
    rwa->age= 23 ; 
    Row* rwb= new Row() ; 
    rwb->id= 2 ; 
    rwb->name= "Samrat" ; 
    rwb->age= 45 ; 
    Row* rwc= new Row() ; 
    rwc->id= 3 ; 
    rwc->name= "Areeba" ; 
    rwc->age= 13 ; 
    st->rows.push_back(rwa ) ; 
    st->rows.push_back(rwb ) ; 
    st->rows.push_back(rwc ) ; 
    /* cout << "here" << endl ; */ 
    Parser* parse= new Parser() ; 
    parse->parseQuery(query ) ; 
    string rowFetch= parse->getResultFromTable(st ) ; 
    cout << rowFetch ; 
} 