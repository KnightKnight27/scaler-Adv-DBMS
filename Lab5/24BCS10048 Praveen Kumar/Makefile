# ==============================================================================
#  Red-Black Tree -- Makefile
# ==============================================================================
#
#  Build a red-black tree implementation (CLRS chapter 13).
#
#  Usage:
#    make          Build the binary
#    make run      Build and run
#    make clean    Remove build artifacts
# ==============================================================================

CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2
TARGET   := rb_tree
SRC      := rb_tree.cpp

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ $<

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
