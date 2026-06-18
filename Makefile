CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra

SHUNTING_BIN := shunting_yard
SQL_PARSER_BIN := sql_parser

all: $(SHUNTING_BIN) $(SQL_PARSER_BIN)

$(SHUNTING_BIN): djikstra/main.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

$(SQL_PARSER_BIN): query/main.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

run-shunting: $(SHUNTING_BIN)
	./$(SHUNTING_BIN)

run-sql: $(SQL_PARSER_BIN)
	./$(SQL_PARSER_BIN)

clean:
	rm -f $(SHUNTING_BIN) $(SQL_PARSER_BIN)

.PHONY: all run-shunting run-sql clean
