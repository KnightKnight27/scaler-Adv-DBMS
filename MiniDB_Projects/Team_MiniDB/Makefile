# MiniDB Makefile — Member 1 storage build (Members 2 & 3 add their files later)

CXX = g++
CXXFLAGS = -std=c++17 -Wall -Isrc

# Member 1 files (current)
SRCS = src/main.cpp \
       src/storage/Page.cpp src/storage/PageManager.cpp \
       src/storage/HeapFile.cpp src/storage/BufferPool.cpp

minidb: $(SRCS)
	$(CXX) $(CXXFLAGS) -o minidb $(SRCS)

clean:
	rm -f minidb
	rm -rf minidb_data

.PHONY: clean
