# File_Ops

A small C++ program demonstrating low-level POSIX file I/O using the `open`, `write`, `read`, and `close` system calls from `<fcntl.h>` and `<unistd.h>`.

## What it does

1. **Opens** `test.txt` for writing (`O_CREAT | O_WRONLY | O_TRUNC`, mode `0644`) — creating or truncating it.
2. **Writes** the string `"I am writing to this file"` to the file descriptor.
3. **Closes** the write FD.
4. **Re-opens** the same file in read-only mode (`O_RDONLY`).
5. **Reads** up to 255 bytes into a buffer and prints the contents.
6. **Closes** the read FD.

Each step prints a short status line plus a note about what the kernel is doing under the hood (allocating inodes, copying data, updating sizes, etc.).

## Build & Run

```bash
g++ main.cpp -o file_ops
./file_ops
```

## Files

- `main.cpp` — source code
- `test.txt` — created at runtime by the program
