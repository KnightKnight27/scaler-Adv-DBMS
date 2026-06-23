CXX = g++
CXXFLAGS = -std=c++17 -Wall
TARGET = btree
SRCS = BPlusTree.cpp main.cpp

all: $(TARGET)

$(TARGET): $(SRCS) BPlusTree.h
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRCS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) $(TARGET).exe
