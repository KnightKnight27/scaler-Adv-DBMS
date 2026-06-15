CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2
TARGET    = rbtree
OBJS      = main.o RedBlackTree.o

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS)

main.o: main.cc RedBlackTree.h
	$(CXX) $(CXXFLAGS) -c main.cc

RedBlackTree.o: RedBlackTree.cc RedBlackTree.h
	$(CXX) $(CXXFLAGS) -c RedBlackTree.cc

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)
