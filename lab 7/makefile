CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -pthread

all: txmgr

txmgr: txmgr.cpp
	$(CXX) $(CXXFLAGS) -o txmgr txmgr.cpp

run: txmgr
	./txmgr

clean:
	rm -f txmgr
