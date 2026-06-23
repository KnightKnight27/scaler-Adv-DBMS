CC := g++
CFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -O2

TARGET := red_black_tree
SRCS := red-black-tree.cc main.cc

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
