# warp si

A C++ program that demonstrates file read and write operations using **raw Linux system calls only** -- no standard library, no libc, no `printf`, no `fstream`.

## What It Does

1. **Writes** a message to `warpsi_data.txt` using the `write` syscall
2. **Reads** the message back using the `read` syscall
3. **Traces** the full journey of each operation through the OS kernel stack

## Quick Start

```bash
# Build
make

# Run
make run
```

Or manually:

```bash
g++ -nostdlib -static -o warp_si warp_si.cpp
./warp_si
```

## System Calls Used

| Syscall | # | Purpose |
|---------|---|---------|
| `open`  | 2 | Open/create a file |
| `write` | 1 | Write bytes to fd |
| `read`  | 0 | Read bytes from fd |
| `close` | 3 | Release fd |
| `exit`  | 60 | Terminate process |

## Documentation

See [Assignment.md](Assignment.md) for the full journey documentation, including diagrams of the write and read paths through VFS, page cache, I/O scheduler, and block device layers.
Praveen Kumar
24bcs10048

## Requirements

- Linux x86-64
- g++ (any recent version)
