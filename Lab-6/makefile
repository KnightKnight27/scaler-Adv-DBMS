CC := g++
CFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -O2

TARGET := transaction_manager
SRCS := main.cpp transaction_manager.cc lock_manager.cc deadlock_detector.cc
HEADERS := transaction_manager.h lock_manager.h deadlock_detector.h mvcc.h

all: $(TARGET)

$(TARGET): $(SRCS) $(HEADERS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
