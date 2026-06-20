CC     = gcc
CFLAGS = -Wall -Wextra -std=c11 -pedantic
TARGET = rbtree
SRC    = rbtree.c

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
