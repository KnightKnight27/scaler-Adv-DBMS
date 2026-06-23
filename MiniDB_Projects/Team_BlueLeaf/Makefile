# MiniDB build (primary build path; cmake is optional, see build.sh / CMakeLists.txt).
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pthread -Isrc

BUILD    := build
SRC      := $(shell find src -name '*.cpp')
CORE_SRC := $(filter-out src/main.cpp,$(SRC))
CORE_OBJ := $(CORE_SRC:%.cpp=$(BUILD)/%.o)
MAIN_OBJ := $(BUILD)/src/main.o

all: minidb

minidb: $(CORE_OBJ) $(MAIN_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Compile and run every tests/*.cpp against the core objects (assert-based, no framework).
TEST_SRC := $(wildcard tests/*.cpp)
test: $(CORE_OBJ)
	@set -e; for t in $(TEST_SRC); do \
	  name=$$(basename $$t .cpp); \
	  echo "=== build+run $$name ==="; \
	  $(CXX) $(CXXFLAGS) $$t $(CORE_OBJ) -o $(BUILD)/$$name; \
	  $(BUILD)/$$name; \
	done

# Benchmark harness (added in M5).
bench: $(CORE_OBJ)
	$(CXX) $(CXXFLAGS) benchmarks/bench_main.cpp $(CORE_OBJ) -o $(BUILD)/minidb_bench

clean:
	rm -rf $(BUILD) minidb

.PHONY: all test bench clean
