# Makefile for Lab 5 — Shunting-Yard + SQL Parser

CXX      = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra
TARGET   = sql_demo
SRC      = main.cpp

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SRC) shunting_yard.h sql_parser.h
	$(CXX) $(CXXFLAGS) -o $@ $(SRC)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
