# MiniDB build system
CXX      := clang++
# -MMD -MP emit per-object header dependency files so edits to a .h trigger a
# rebuild of every .o that includes it (prevents stale-object ODR mismatches).
CXXFLAGS := -std=c++17 -Wall -Wextra -O2 -g -Isrc -pthread -MMD -MP
LDFLAGS  := -pthread

# All library sources = every .cpp under src/ except the shell entry point.
LIB_SRCS := $(filter-out src/main.cpp,$(wildcard src/*.cpp src/*/*.cpp))
LIB_OBJS := $(LIB_SRCS:.cpp=.o)

TEST_SRCS := $(wildcard tests/*.cpp)
TEST_BINS := $(TEST_SRCS:tests/%.cpp=build/%)

.PHONY: all run-tests bench clean run

all: minidb

# --- SQL shell -------------------------------------------------------------
minidb: src/main.o $(LIB_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

run: minidb
	./minidb

# --- object compilation ----------------------------------------------------
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# --- tests -----------------------------------------------------------------
build/%: tests/%.cpp $(LIB_OBJS)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $< $(LIB_OBJS) -o $@ $(LDFLAGS)

run-tests: $(TEST_BINS)
	@pass=0; fail=0; \
	for t in $(TEST_BINS); do \
		echo "=================================================="; \
		echo "RUN  $$t"; \
		if ./$$t; then pass=$$((pass+1)); else fail=$$((fail+1)); echo "FAIL $$t"; fi; \
	done; \
	echo "=================================================="; \
	echo "SUITES: $$pass passed, $$fail failed"; \
	[ $$fail -eq 0 ]

# --- benchmark -------------------------------------------------------------
bench: benchmarks/bench.cpp $(LIB_OBJS)
	$(CXX) $(CXXFLAGS) $< $(LIB_OBJS) -o bench $(LDFLAGS)
	./bench

clean:
	rm -f minidb bench src/*.o src/*/*.o src/*.d src/*/*.d *.d
	rm -rf build *.dSYM
	rm -f *.db *.catalog *.wal /tmp/minidb_*

# Pull in auto-generated header dependencies.
-include $(LIB_SRCS:.cpp=.d) src/main.d $(TEST_SRCS:tests/%.cpp=build/%.d)
