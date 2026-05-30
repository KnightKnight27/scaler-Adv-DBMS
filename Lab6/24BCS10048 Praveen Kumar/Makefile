# ==============================================================================
#  B-Tree -- Makefile
# ==============================================================================
#
#  Build a B-tree implementation (CLRS chapter 18).
#
#  Usage:
#    make          Build the binary
#    make run      Build and run
#    make clean    Remove build artifacts
# ==============================================================================

CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2
TARGET   := btree
SRC      := btree.cpp

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ $<

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
