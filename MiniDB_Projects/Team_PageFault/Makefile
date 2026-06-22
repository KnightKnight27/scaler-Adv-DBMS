# MiniDB build — C++17, no external dependencies (stdlib + pthreads only).
CXX      := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -pthread -Isrc
BIN      := bin

# All engine sources except the REPL entry point.
SRCS := $(filter-out src/main.cpp,$(shell find src -name '*.cpp'))
OBJS := $(SRCS:.cpp=.o)

.PHONY: all clean bench run

all: $(BIN)/minidb $(BIN)/bench

# The SQL REPL.
$(BIN)/minidb: $(OBJS) src/main.o | $(BIN)
	$(CXX) $(CXXFLAGS) $^ -o $@

# The MVCC-vs-2PL benchmark (only needs the txn engine).
$(BIN)/bench: benchmarks/bench_mvcc_vs_2pl.o src/txn/transaction_manager.o | $(BIN)
	$(CXX) $(CXXFLAGS) $^ -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BIN):
	mkdir -p $(BIN)

run: $(BIN)/minidb
	./$(BIN)/minidb

bench: $(BIN)/bench
	./$(BIN)/bench

clean:
	rm -rf $(BIN) $(OBJS) src/main.o benchmarks/*.o *.dat *.cat *.wal
