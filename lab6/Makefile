# Makefile for Lab 6 — Transaction Manager

CXX      = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -pthread
TARGET   = txn_demo
SRC      = main.cpp

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SRC) mvcc.h lock_manager.h deadlock_detector.h transaction_manager.h
	$(CXX) $(CXXFLAGS) -o $@ $(SRC)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
