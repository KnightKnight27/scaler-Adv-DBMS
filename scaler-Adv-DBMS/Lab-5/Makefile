CC       := g++
CFLAGS   := -std=c++17 -Wall -Werror -O3
BIN      := bst_runner
SRCS     := main.cc RedBlackTree.cc

.PHONY: all execute clean

all: $(BIN)

$(BIN): $(SRCS) RedBlackTree.h
	$(CC) $(CFLAGS) -o $(BIN) $(SRCS)

execute: $(BIN)
	./$(BIN)

clean:
	rm -f $(BIN)
