CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2

all: rbt btree

rbt: rbt.cpp
	$(CXX) $(CXXFLAGS) -o rbt rbt.cpp

btree: btree.cpp
	$(CXX) $(CXXFLAGS) -o btree btree.cpp

run: all
	./rbt
	./btree

clean:
	rm -f rbt btree

.PHONY: all run clean
