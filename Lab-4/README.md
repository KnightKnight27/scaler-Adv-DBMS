# Lab 4

This lab contains three separate C++ programs:

- `b-tree`: full B-Tree with insert, split, borrow, merge, and delete
- `red black tree`: Red-Black Tree with insertion and deletion
- `hex dump`: a small hex dump utility

## Build with CMake

```bash
cd /Users/ayaansingh_03/Desktop/A-DBMS/Lab-4
cmake -S . -B build
cmake --build build
```

## Run

```bash
./build/b_tree
./build/red_black_tree
./build/hex_dump <file>
```

The B-Tree and Red-Black Tree executables print sample insert/delete operations.
The hex dump utility prints the contents of a file in hex + ASCII format.