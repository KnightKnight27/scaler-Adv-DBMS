CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -pthread -O3
TARGET   := txn_manager
SOURCES  := main.cpp

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SOURCES)

run: $(TARGET)
	./$(TARGET)

test: run

clean:
	rm -f $(TARGET)

.PHONY: all run test clean
