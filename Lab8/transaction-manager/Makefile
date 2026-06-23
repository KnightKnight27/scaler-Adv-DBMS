CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -pthread -O2
TARGET = tx_manager
SOURCE = main.cpp

.PHONY: build run test clean

build:
	$(CXX) $(CXXFLAGS) $(SOURCE) -o $(TARGET)

run: build
	./$(TARGET)

test: build
	./$(TARGET) --test

clean:
	rm -f $(TARGET)
