# ============================================================================
# MiniDB Makefile
#
#   make          -> build the `minidb` REPL binary (build/minidb)
#   make bench    -> build the benchmark binary       (build/bench)
#   make run      -> build and start the REPL
#   make clean    -> remove all build artifacts
#
# We use a plain Makefile (no CMake) so the build is easy to read: it simply
# compiles every .cpp under src/ and links them together.
# ============================================================================
CXX      := c++
# -Wno-unqualified-std-cast-call: we use `using namespace std;`, so move()/forward()
# are written without the std:: prefix like the rest of the code.
CXXFLAGS := -std=c++17 -Wall -Wextra -O2 -Isrc -pthread -Wno-unqualified-std-cast-call
BUILD    := build

# Library = every source file except the three entry points.
ENTRIES  := src/main.cpp src/bench.cpp src/demo_concurrency.cpp
LIB_SRCS := $(filter-out $(ENTRIES),$(shell find src -name '*.cpp'))
LIB_OBJS := $(patsubst src/%.cpp,$(BUILD)/%.o,$(LIB_SRCS))

all: $(BUILD)/minidb

$(BUILD)/minidb: $(LIB_OBJS) $(BUILD)/main.o
	$(CXX) $(CXXFLAGS) $^ -o $@
	@echo "built $@"

bench: $(BUILD)/bench
$(BUILD)/bench: $(LIB_OBJS) $(BUILD)/bench.o
	$(CXX) $(CXXFLAGS) $^ -o $@
	@echo "built $@"

# Concurrency demo: 2PL blocking + deadlock detection.
demo: $(BUILD)/demo_concurrency
$(BUILD)/demo_concurrency: $(LIB_OBJS) $(BUILD)/demo_concurrency.o
	$(CXX) $(CXXFLAGS) $^ -o $@
	@echo "built $@"

# Pattern rule: compile src/foo/bar.cpp -> build/foo/bar.o
$(BUILD)/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

run: all
	./$(BUILD)/minidb

clean:
	rm -rf $(BUILD)

.PHONY: all bench run clean
