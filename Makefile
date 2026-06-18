# Lab 5 - Shunting-Yard Evaluator + SQL SELECT Parser


CXX      := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wpedantic -Wshadow

SHUNTING_BIN := dijkstraShunting/shunting
PARSER_BIN   := queryParsing/parser

.PHONY: all run run-shunting run-parser clean

all: $(SHUNTING_BIN) $(PARSER_BIN)

$(SHUNTING_BIN): dijkstraShunting/main.cpp
$(CXX) $(CXXFLAGS) -o $@ $<

$(PARSER_BIN): queryParsing/main.cpp
$(CXX) $(CXXFLAGS) -o $@ $<

run: run-shunting run-parser

run-shunting: $(SHUNTING_BIN)
@echo "======================================"
@echo "Dijkstra Shunting-Yard Evaluator"
@echo "======================================"
@./$(SHUNTING_BIN)

run-parser: $(PARSER_BIN)
@echo "======================================"
@echo "SQL SELECT Parser + WHERE Evaluator"
@echo "======================================"
@./$(PARSER_BIN)

clean:
rm -f $(SHUNTING_BIN) $(PARSER_BIN)
