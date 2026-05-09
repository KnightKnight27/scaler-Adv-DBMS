CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra
TARGET = task1
SRC = file_syscalls.cpp

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET)

clean:
	rm -f $(TARGET) input.txt output.txt

.PHONY: all clean
