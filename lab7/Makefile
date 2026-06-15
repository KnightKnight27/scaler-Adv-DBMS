CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2

TARGET = sql_parser
SOURCES = main.cpp lexer.cpp
HEADERS = types.h lexer.h parser.h executor.h expressions.h
OBJECTS = $(SOURCES:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $^

%.o: %.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(OBJECTS) $(TARGET)

.PHONY: all run clean
