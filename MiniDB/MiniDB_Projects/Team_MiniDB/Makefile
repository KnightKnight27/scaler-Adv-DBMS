# MiniDB — build with `make`, run the demo with `make run`.
CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -pthread -O2 -Isrc
# Static link: produces a standalone binary and avoids a libstdc++/libgcc DLL
# resolution failure seen with dynamic linking under MSYS2/MinGW.
LDFLAGS  := -static
SRCS     := $(shell find src -name '*.cpp')
OBJS     := $(SRCS:.cpp=.o)
BIN      := minidb

.PHONY: all run clean

all: $(BIN)

$(BIN): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

run: $(BIN)
	./$(BIN)

clean:
	rm -f $(OBJS) $(BIN) minidb_data.db
