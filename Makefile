# Compiler and flags
CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++17 -O2

# Targets and objects
TARGET = rbt
SRCS = main.cc RedBlackTree.cc
OBJS = $(SRCS:.cc=.o)

# Default target
all: $(TARGET)

# Compile target executable
$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS)

# Compile object files
%.o: %.cc RedBlackTree.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean target
clean:
	rm -f $(TARGET) $(OBJS)

# Phony targets
.PHONY: all clean
