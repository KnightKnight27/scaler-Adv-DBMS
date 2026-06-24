CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2

sql_parser: sql_parser.cpp
	$(CXX) $(CXXFLAGS) -o sql_parser sql_parser.cpp

run: sql_parser
	./sql_parser

clean:
	rm -f sql_parser

.PHONY: run clean
