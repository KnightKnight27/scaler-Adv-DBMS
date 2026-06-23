CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2
TARGET   := shunting_yard

all: $(TARGET)

$(TARGET): main.cpp
	$(CXX) $(CXXFLAGS) -o $(TARGET) main.cpp

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all run clean
