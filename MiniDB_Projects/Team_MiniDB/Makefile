CXX = g++
CXXFLAGS = -std=c++17 -Wall -Isrc
SRCS = src/main.cpp src/Database.cpp \
       src/storage/Page.cpp src/storage/PageManager.cpp src/storage/HeapFile.cpp src/storage/BufferPool.cpp \
       src/index/BPlusTree.cpp src/catalog/Catalog.cpp \
       src/parser/Lexer.cpp src/parser/Parser.cpp \
       src/optimizer/Optimizer.cpp src/execution/Executor.cpp \
       src/transaction/LockManager.cpp src/transaction/TransactionManager.cpp \
       src/recovery/WAL.cpp src/recovery/RecoveryManager.cpp

minidb: $(SRCS)
	$(CXX) $(CXXFLAGS) -o minidb $(SRCS)

clean:
	rm -f minidb
	rm -rf minidb_data

.PHONY: clean
