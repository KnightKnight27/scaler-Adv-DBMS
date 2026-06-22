# ==============================================================================
#  warp si -- Makefile
# ==============================================================================
#
#  Build a C++ program that uses ONLY raw Linux system calls.
#  No libc, no C runtime -- the kernel is the only dependency.
#
#  Usage:
#    make          Build the binary
#    make run      Build and run
#    make clean    Remove build artifacts
# ==============================================================================

CXX      := g++
CXXFLAGS := -nostdlib -static -Wall -Wextra -O2
TARGET   := warp_si
SRC      := warp_si.cpp

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ $<

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) warpsi_data.txt
