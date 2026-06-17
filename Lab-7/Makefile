# ADBMS Lab 7 — Shunting-Yard + SQL SELECT engine (24BCS10115 Gauri Shukla)
# `make` builds ./sql_demo ; `make run` builds and runs it.

CXX      ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic -Wshadow
TARGET   := sql_demo
OBJS     := main.o sql_engine.o

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS)

main.o: main.cc sql_engine.h
	$(CXX) $(CXXFLAGS) -c main.cc

sql_engine.o: sql_engine.cc sql_engine.h
	$(CXX) $(CXXFLAGS) -c sql_engine.cc

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) $(OBJS)
