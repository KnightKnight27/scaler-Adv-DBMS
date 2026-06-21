# MiniDB — build with `make`, run the demo with `make run`.
CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -pthread -O2 -Isrc
# Static link: produces a standalone binary and avoids a libstdc++/libgcc DLL
# resolution failure seen with dynamic linking under MSYS2/MinGW.
LDFLAGS  := -static
SRCS     := $(shell find src -name '*.cpp')
OBJS     := $(SRCS:.cpp=.o)
BIN      := minidb

BENCH_SRC := benchmarks/bench_mvcc_vs_2pl.cpp src/txn/transaction_manager.cpp
BENCH_BIN := benchmarks/bench

.PHONY: all run bench clean

all: $(BIN)

$(BIN): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

run: $(BIN)
	./$(BIN)

# Track B benchmark: MVCC vs 2PL under contention.
bench: $(BENCH_SRC)
	$(CXX) $(CXXFLAGS) -o $(BENCH_BIN) $(BENCH_SRC) $(LDFLAGS)
	./$(BENCH_BIN)

clean:
	rm -f $(OBJS) $(BIN) $(BENCH_BIN) minidb_data.db *.db
