# ADBMS Lab 8 — Transaction Manager: MVCC + 2PL (24BCS10115 Gauri Shukla)
# `make` builds ./txmgr_demo ; `make run` builds and runs it.

CXX      ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic -Wshadow
TARGET   := txmgr_demo
OBJS     := main.o txn_manager.o

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS)

main.o: main.cc txn_manager.h
	$(CXX) $(CXXFLAGS) -c main.cc

txn_manager.o: txn_manager.cc txn_manager.h
	$(CXX) $(CXXFLAGS) -c txn_manager.cc

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) $(OBJS)
