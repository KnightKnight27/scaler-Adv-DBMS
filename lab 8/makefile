CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2

all: lab8_demo

lab8_demo: main.cpp db_engine.cpp db_engine.hpp
	$(CXX) $(CXXFLAGS) -o lab8_demo main.cpp db_engine.cpp

run: lab8_demo
	./lab8_demo

clean:
	rm -f lab8_demo
	rm -rf build

cmake-build:
	mkdir -p build && cd build && cmake .. && cmake --build .

.PHONY: all run clean cmake-build
