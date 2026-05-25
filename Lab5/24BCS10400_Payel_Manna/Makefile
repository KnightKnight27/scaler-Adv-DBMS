CXX = g++
CXXFLAGS = -std=c++17 -Wall

SRC = src/main.cpp src/RedBlackTree.cpp

OUT = build/rbt

all:
	mkdir -p build
	$(CXX) $(CXXFLAGS) $(SRC) -o $(OUT)

run: all
	./$(OUT)

clean:
	rm -rf build