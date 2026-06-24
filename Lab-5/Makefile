CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2
TARGET   := rbtree
SOURCES  := main.cc RedBlackTree.cc

all: $(TARGET)

$(TARGET): $(SOURCES) RedBlackTree.h
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SOURCES)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all run clean
