# ADBMS Lab 6 — Transaction Processing Engine (24bcs10213 Jatin Chulet)
# Run `make` to compile ; `make run` to execute the suite.

CXX      ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic -Wshadow
TARGET   := tx_engine_test
OBJS     := main.o transaction_engine.o

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS)

main.o: main.cpp transaction_engine.h
	$(CXX) $(CXXFLAGS) -c main.cpp

transaction_engine.o: transaction_engine.cpp transaction_engine.h
	$(CXX) $(CXXFLAGS) -c transaction_engine.cpp

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) $(OBJS)