# Lab 5 - Red-Black Tree
# Bhavya Jain (23BCS10088)

CXX      := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wpedantic -Wshadow
TARGET   := rbtree
OBJS     := RedBlackTree.o main.o

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

RedBlackTree.o: RedBlackTree.cpp RedBlackTree.h
	$(CXX) $(CXXFLAGS) -c RedBlackTree.cpp

main.o: main.cpp RedBlackTree.h
	$(CXX) $(CXXFLAGS) -c main.cpp

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) $(OBJS)
