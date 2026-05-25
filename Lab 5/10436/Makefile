CXX     = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2

TARGET  = red_black_tree
SRCS    = RedBlackTree.cpp main.cpp
OBJS    = $(SRCS:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cpp RedBlackTree.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

run: all
	./$(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all run clean
