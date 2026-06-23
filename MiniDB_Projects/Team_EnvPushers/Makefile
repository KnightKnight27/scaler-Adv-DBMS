# MiniDB build. Pure g++/make, C++17, no external dependencies.
CXX      := g++
# -MMD -MP emit per-object header dependency files so edits to a .hpp trigger a
# rebuild of every .o that includes it (prevents stale-object layout mismatches).
CXXFLAGS := -std=c++17 -Isrc -O2 -Wall -Wextra -Wno-unused-parameter -pthread -MMD -MP
LDFLAGS  := -pthread

SRC_DIR     := src
BUILD_DIR   := build
LIB_SRCS    := $(shell find $(SRC_DIR) -name '*.cpp')
LIB_OBJS    := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(LIB_SRCS))

BIN_DIR := bin

.PHONY: all clean repl demo bench test dirs

all: dirs $(BIN_DIR)/minidb $(BIN_DIR)/demo $(BIN_DIR)/bench_lsm $(BIN_DIR)/tests

dirs:
	@mkdir -p $(BUILD_DIR) $(BIN_DIR)

# Library object files.
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Executables.
$(BIN_DIR)/minidb: tools/repl.cpp $(LIB_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/demo: tools/demo.cpp $(LIB_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/bench_lsm: benchmarks/bench_lsm.cpp $(LIB_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/tests: tests/run_tests.cpp $(LIB_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

repl: $(BIN_DIR)/minidb
	./$(BIN_DIR)/minidb

demo: $(BIN_DIR)/demo
	./$(BIN_DIR)/demo

bench: $(BIN_DIR)/bench_lsm
	./$(BIN_DIR)/bench_lsm

test: $(BIN_DIR)/tests
	./$(BIN_DIR)/tests

# Compile just the library objects (used during development).
lib: dirs $(LIB_OBJS)

# Pull in auto-generated header dependencies.
-include $(LIB_OBJS:.o=.d)

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR) *.db *.wal data bench_lsm_data data_test_*
