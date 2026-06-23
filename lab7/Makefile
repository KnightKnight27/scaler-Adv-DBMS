CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2
TARGET = select_parser
SRCS = main.cpp db_engine.cpp shunting_yard.cpp sql_parser.cpp
OBJS = $(SRCS:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)
