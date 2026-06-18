# Lab 6 - B-Tree
# Rama Krishnan (24BCS10087)

CXX      := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wpedantic -Wshadow
TARGET   := btree
TEST     := test_runner

.PHONY: all run test clean

all: $(TARGET)

$(TARGET): main.cpp btree.cpp btree.h
	$(CXX) $(CXXFLAGS) -o $(TARGET) main.cpp btree.cpp

$(TEST): test.cpp btree.cpp btree.h
	$(CXX) $(CXXFLAGS) -DTESTING -o $(TEST) test.cpp btree.cpp

run: $(TARGET)
	./$(TARGET)

test: $(TEST)
	./$(TEST)

clean:
	rm -f $(TARGET) $(TEST)
