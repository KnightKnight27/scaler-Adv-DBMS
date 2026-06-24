CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -pthread

all: wal_demo

wal_demo: main.cpp wal_manager.cpp
	$(CXX) $(CXXFLAGS) -o wal_demo main.cpp

run: wal_demo
	./wal_demo

clean:
	rm -f wal_demo wal.log

.PHONY: all run clean
