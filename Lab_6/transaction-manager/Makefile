# Makefile for Lab 6 Transaction Manager
# Course: Advanced DBMS
# Author: Abdul Kalam Azad (24BCS10053)

CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -pthread -O3
TARGET = txmgr
SRC = main.cpp

.PHONY: all build run test clean

all: build

build: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET)

run: build
	./$(TARGET)

test: build
	./$(TARGET) --test

clean:
	rm -f $(TARGET)
