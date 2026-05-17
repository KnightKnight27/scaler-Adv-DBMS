# ==============================================================================
#  Makefile -- syscall_io
#  Author: Anushka Jain | 24BCS10193
# ==============================================================================
#
#  Builds a C++ program using ONLY raw Linux syscalls.
#  Flags:
#    -nostdlib   : do not link against any standard library
#    -static     : fully statically linked (no dynamic loader needed)
#    -Wall -Wextra -O2 : warnings + optimization
#
#  Usage:
#    make          Build the binary
#    make run      Build and execute
#    make clean    Remove build artifacts
# ==============================================================================

CXX      := g++
CXXFLAGS := -nostdlib -static -Wall -Wextra -O2
TARGET   := syscall_io
SRC      := syscall_io.cpp

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ $<
	@echo "Build OK -> ./$(TARGET)"

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) syscall_io_data.txt
