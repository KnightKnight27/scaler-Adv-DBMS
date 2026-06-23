CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2

all: btree

btree: btree.cpp
	$(CXX) $(CXXFLAGS) -o btree btree.cpp

run: btree
	./btree

clean:
	rm -f btree
