CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2

SRC = main.cc shunting_yard.cc sql.cc
BIN = sqlmini

$(BIN): $(SRC) shunting_yard.h sql.h
	$(CXX) $(CXXFLAGS) $(SRC) -o $(BIN)

run: $(BIN)
	./$(BIN)

clean:
	rm -f $(BIN) *.o

.PHONY: run clean
