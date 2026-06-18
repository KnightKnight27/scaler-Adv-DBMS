CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -pthread
TARGET = tx_manager

all: build

build: $(TARGET)

$(TARGET): main.cpp
	$(CXX) $(CXXFLAGS) main.cpp -o $(TARGET)

run: build
	./$(TARGET)

clean:
	$(RM) $(TARGET) $(TARGET).exe
