# MiniDB build file.
# The engine is header-only (everything under src/), so we just compile the two
# entry points: the REPL (main.cpp) and the benchmark harness.

CXX      ?= clang++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pthread -I.

BIN       = minidb
BENCH_BIN = minidb_bench

.PHONY: all clean run demo bench

all: $(BIN) $(BENCH_BIN)

$(BIN): main.cpp $(wildcard src/**/*.h) $(wildcard src/*.h)
	$(CXX) $(CXXFLAGS) -o $(BIN) main.cpp

$(BENCH_BIN): benchmarks/benchmark.cpp $(wildcard src/**/*.h) $(wildcard src/*.h)
	$(CXX) $(CXXFLAGS) -o $(BENCH_BIN) benchmarks/benchmark.cpp

run: $(BIN)
	./$(BIN)

demo: $(BIN)
	./$(BIN) demo

bench: $(BENCH_BIN)
	./$(BENCH_BIN)

clean:
	rm -f $(BIN) $(BENCH_BIN) *.db *.wal *.catalog
	rm -f /tmp/minidb_demo.* /tmp/minidb_crash.* /tmp/minidb_bench.*
