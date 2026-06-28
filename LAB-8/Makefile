# Makefile for Lab 8 Transaction Manager
# Course: Advanced DBMS
# Author: Bhavya Jain (23BCS10088) <Bhavya.23bcs10088@sst.scaler.com>

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
