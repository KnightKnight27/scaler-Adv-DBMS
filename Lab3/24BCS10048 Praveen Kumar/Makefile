# ==============================================================================
#  Clock Sweep -- Makefile
# ==============================================================================
#
#  Build a clock sweep buffer pool replacement demo.
#
#  Usage:
#    make          Build the binary
#    make run      Build and run
#    make clean    Remove build artifacts
# ==============================================================================

CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2
TARGET   := clock_sweep
SRC      := clock_sweep.cpp

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ $<

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
