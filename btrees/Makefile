CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2
TARGET   := btree
SRC      := main.cpp

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: run clean
