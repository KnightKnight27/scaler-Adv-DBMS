#include <iostream>
#include <stack>
#include <string>
#include <cctype>
using namespace std;

int precedence(char op) {
    if(op=='+' || op=='-') return 1;
    if(op=='*' || op=='/') return 2;
    return 0;
}

string infixToPostfix(string exp) {
    stack<char> st;
    string postfix;

    for(char ch : exp) {
        if(ch==' ') continue;

        if(isdigit(ch))
            postfix += ch;

        else if(ch=='(')
            st.push(ch);

        else if(ch==')') {
            while(!st.empty() && st.top()!='(') {
                postfix += st.top();
                st.pop();
            }
            st.pop();
        }

        else {
            while(!st.empty() &&
                  precedence(st.top()) >= precedence(ch)) {
                postfix += st.top();
                st.pop();
            }
            st.push(ch);
        }
    }

    while(!st.empty()) {
        postfix += st.top();
        st.pop();
    }

    return postfix;
}

int evaluatePostfix(string postfix) {
    stack<int> st;

    for(char ch : postfix) {

        if(isdigit(ch))
            st.push(ch-'0');

        else {
            int b = st.top(); st.pop();
            int a = st.top(); st.pop();

            switch(ch) {
                case '+': st.push(a+b); break;
                case '-': st.push(a-b); break;
                case '*': st.push(a*b); break;
                case '/': st.push(a/b); break;
            }
        }
    }

    return st.top();
}

int main() {

    string expr = "3+4*2";

    string postfix = infixToPostfix(expr);

    cout<<"Infix: "<<expr<<endl;
    cout<<"Postfix: "<<postfix<<endl;
    cout<<"Result: "<<evaluatePostfix(postfix)<<endl;

    return 0;
}