# ADBMS Lab 5 — Shunting-Yard + SQL SELECT engine
# 24bcs10632  Patel Jash
# Usage: `make` to compile ./sql_demo  |  `make run` to compile and execute

CXX      ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic -Wshadow
TARGET   := sql_demo
OBJS     := main.o sql_engine.o

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS)

main.o: main.cpp sql_engine.h
	$(CXX) $(CXXFLAGS) -c main.cpp

sql_engine.o: sql_engine.cpp sql_engine.h
	$(CXX) $(CXXFLAGS) -c sql_engine.cpp

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) $(OBJS)
