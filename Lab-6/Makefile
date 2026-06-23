CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2 -pthread

SRC = main.cc transaction_manager.cc
BIN = txnmgr

$(BIN): $(SRC) transaction_manager.h
	$(CXX) $(CXXFLAGS) $(SRC) -o $(BIN)

run: $(BIN)
	./$(BIN)

clean:
	rm -f $(BIN) *.o

.PHONY: run clean
