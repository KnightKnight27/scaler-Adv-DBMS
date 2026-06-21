CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2
TARGET   := btree
SOURCES  := main.cpp

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SOURCES)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all run clean
