# Makefile for Lab 1 — C++ File I/O with strace
# ──────────────────────────────────────────────

CXX      = g++
CXXFLAGS = -std=c++17 -O0 -g -Wall -Wextra
TARGET   = file_io_demo
SRC      = file_io_demo.cpp

.PHONY: all clean run trace

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ $<

run: $(TARGET)
	./$(TARGET)

trace: $(TARGET)
	@echo "Running under strace..."
	strace -f -tt -T \
		-e trace=openat,read,write,lseek,fsync,mmap,munmap,close,fstat,msync,unlink \
		-o strace_output.log \
		./$(TARGET)
	@echo "Trace saved to strace_output.log"

clean:
	rm -f $(TARGET) testfile.dat iostream_test.dat direct_io_test.dat strace_output.log
