CXX      := c++
CXXFLAGS := -std=c++17 -O3 -Wall -Wextra -Wpedantic -Wshadow

TARGET   := mini_sql_engine
OBJECTS  := main.o sql_engine.o

.PHONY: all default run clean

all: $(TARGET)

default: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJECTS)

main.o: main.cpp sql_engine.h
	$(CXX) $(CXXFLAGS) -c main.cpp -o $@

sql_engine.o: sql_engine.cpp sql_engine.h
	$(CXX) $(CXXFLAGS) -c sql_engine.cpp -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) $(OBJECTS)