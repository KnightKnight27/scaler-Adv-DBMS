CXX = g++
CXXFLAGS = -std=c++17 -Wall

all: rbt

rbt: main.cpp RedBlackTree.cc RedBlackTree.h
	$(CXX) $(CXXFLAGS) main.cpp RedBlackTree.cc -o rbt

clean:
	rm -f rbt
