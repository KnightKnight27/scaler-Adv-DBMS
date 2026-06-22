# ==============================================================================
#  Lab 7 -- SQL Query Parser & Shunting-Yard Evaluator -- Makefile
# ==============================================================================

CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2

.PHONY: all run clean

all: query_parser shunting_yard

query_parser: query_parser.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

shunting_yard: shunting_yard.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

run: all
	./query_parser
	./shunting_yard

clean:
	rm -f query_parser shunting_yard
