CC := g++
CFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -O2

TARGET := query_parser
SRCS := main.cpp

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
