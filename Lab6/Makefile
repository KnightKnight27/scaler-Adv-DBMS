CXX = g++
CXXFLAGS = -std=c++17 -Wall

all: btree

btree: main.cpp BTree.cc BTree.h
	$(CXX) $(CXXFLAGS) main.cpp BTree.cc -o btree

clean:
	rm -f btree
