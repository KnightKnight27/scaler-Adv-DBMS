CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2

reader: main.cpp
	$(CXX) $(CXXFLAGS) -o reader main.cpp

run: reader
	./reader test.txt

# Trace the syscall boundary (Linux):
trace: reader
	strace -e trace=openat,read,close,fstat,mmap ./reader

clean:
	rm -f reader

.PHONY: run trace clean
