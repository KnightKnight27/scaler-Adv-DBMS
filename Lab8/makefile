CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2

all: db_simulator

db_simulator: main.cpp db_engine.cpp db_engine.hpp
	$(CXX) $(CXXFLAGS) -o db_simulator main.cpp db_engine.cpp

run: db_simulator
	./db_simulator

clean:
	rm -f db_simulator
	rm -rf build

cmake-build:
	mkdir -p build && cd build && cmake .. && cmake --build .

.PHONY: all run clean cmake-build
