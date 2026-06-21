# MiniDB build file. Requires a C++17 compiler (g++ or clang++) and make.
# No external dependencies.
#
#   make            build the SQL shell ./minidb
#   make test       build and run every unit-test suite
#   make demos      build the deadlock and crash-recovery demos
#   make bench      build the index and MVCC benchmarks
#   make clean      remove build artifacts and generated data

CXX      = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -Isrc

MODULES = src/storage.cpp src/btree.cpp src/catalog.cpp src/parser.cpp \
          src/optimizer.cpp src/executor.cpp src/engine.cpp \
          src/txn.cpp src/recovery.cpp src/mvcc.cpp

TESTS = test_storage test_btree test_query test_txn test_recovery test_mvcc

minidb: src/main.cpp $(MODULES)
	$(CXX) $(CXXFLAGS) -o minidb src/main.cpp $(MODULES)

test: $(MODULES)
	@for t in $(TESTS); do \
	  echo "== $$t =="; \
	  $(CXX) $(CXXFLAGS) -Itests -o $$t tests/$$t.cpp $(MODULES) && ./$$t || exit 1; \
	done

demos: $(MODULES)
	$(CXX) $(CXXFLAGS) -o deadlock_demo demos/deadlock_demo.cpp $(MODULES)
	$(CXX) $(CXXFLAGS) -o recovery_demo demos/recovery_demo.cpp $(MODULES)
	./deadlock_demo
	./recovery_demo

bench: $(MODULES)
	$(CXX) $(CXXFLAGS) -o bench_index benchmarks/bench_index.cpp $(MODULES)
	$(CXX) $(CXXFLAGS) -o bench_mvcc benchmarks/bench_mvcc.cpp $(MODULES)
	./bench_index
	./bench_mvcc

clean:
	rm -f minidb $(TESTS) deadlock_demo recovery_demo bench_index bench_mvcc
	rm -rf team_cash_data bench_data_* *.tbl *.wal

.PHONY: test demos bench clean
