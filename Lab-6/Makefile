# ADBMS Lab 6 — B-Tree (24BCS10115 Gauri Shukla)
# `make` builds ./btree_demo ; `make run` builds and runs it.

CXX      ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic -Wshadow
TARGET   := btree_demo
OBJS     := main.o BTree.o

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS)

main.o: main.cc BTree.h
	$(CXX) $(CXXFLAGS) -c main.cc

BTree.o: BTree.cc BTree.h
	$(CXX) $(CXXFLAGS) -c BTree.cc

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) $(OBJS)
