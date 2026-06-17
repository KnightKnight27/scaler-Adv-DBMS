CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2
TARGET   := btree

all: $(TARGET)

$(TARGET): main.cpp btree.cpp
	$(CXX) $(CXXFLAGS) -o $(TARGET) main.cpp btree.cpp

run: $(TARGET)
	./$(TARGET)

test: test_runner
	./test_runner

test_runner: test.cpp btree.cpp
	$(CXX) $(CXXFLAGS) -DTESTING -o test_runner test.cpp btree.cpp

clean:
	rm -f $(TARGET) test_runner

.PHONY: all run test clean
