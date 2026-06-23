CXX = g++
CXXFLAGS = -std=c++17 -Wall
TARGET = sql_eval
SRCS = ShuntingYard.cpp SQLParser.cpp main.cpp

all: $(TARGET)

$(TARGET): $(SRCS) Row.h ShuntingYard.h SQLParser.h
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRCS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) $(TARGET).exe
