CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2
TARGET   := recursive_descent

all: $(TARGET)

$(TARGET): main.cpp
	$(CXX) $(CXXFLAGS) -o $(TARGET) main.cpp

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all run clean
