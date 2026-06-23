CXX = g++
CXXFLAGS = -std=c++17 -Wall
TARGET = file_io

all: $(TARGET)

$(TARGET): file_io.cpp
	$(CXX) $(CXXFLAGS) -o $(TARGET) file_io.cpp

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) $(TARGET).exe lab1_output.txt
