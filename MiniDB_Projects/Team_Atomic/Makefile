# MiniDB build. Zero external dependencies -- just a C++17 compiler.
CXX      := c++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2 -Isrc -pthread
LDFLAGS  := -pthread

BUILD := build
SRCS  := $(shell find src -name '*.cpp')
OBJS  := $(patsubst src/%.cpp,$(BUILD)/%.o,$(SRCS))

# Test programs live under tests/ and each becomes its own binary.
TEST_SRCS := $(wildcard tests/*.cpp)
TEST_BINS := $(patsubst tests/%.cpp,$(BUILD)/tests/%,$(TEST_SRCS))

# Engine objects shared by the shell and the tests (everything except main).
LIB_OBJS := $(filter-out $(BUILD)/main.o,$(OBJS))

all: minidb

minidb: $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o minidb $(LDFLAGS)

$(BUILD)/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Build & run all tests.
tests: $(TEST_BINS)

$(BUILD)/tests/%: tests/%.cpp $(LIB_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $< $(LIB_OBJS) -o $@ $(LDFLAGS)

run-tests: tests
	@for t in $(TEST_BINS); do echo "=== $$t ==="; $$t || exit 1; done

bench: $(BUILD)/bench
	./$(BUILD)/bench

$(BUILD)/bench: benchmarks/bench.cpp $(LIB_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $< $(LIB_OBJS) -o $@ $(LDFLAGS)

clean:
	rm -rf $(BUILD) minidb *.db *.wal *.catalog *.sst *.manifest

.PHONY: all tests run-tests bench clean
