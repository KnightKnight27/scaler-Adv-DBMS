# Makefile for Lab 4 — Red-Black Tree + B-Tree

CXX      = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra
TARGET   = tree_demo
SRC      = main.cpp

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SRC) rbtree.h btree.h
	$(CXX) $(CXXFLAGS) -o $@ $(SRC)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
