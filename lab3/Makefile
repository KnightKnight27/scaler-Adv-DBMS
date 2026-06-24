# Makefile for Lab 3 — ClockSweep Buffer Pool

CXX      = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra
TARGET   = clock_sweep_demo
SRC      = main.cpp

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SRC) clock_sweep.h
	$(CXX) $(CXXFLAGS) -o $@ $(SRC)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
