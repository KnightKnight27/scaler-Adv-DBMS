# sysCall

Reads and writes a file using raw Linux system calls. No libc, no streams, no printf — just `syscall()` talking directly to the kernel.

## why bother

When you use `fopen` or `std::fstream`, a lot happens between your code and the actual disk that you never see. Here nothing is hidden. Every step you see in the code is a literal crossing of the user/kernel boundary.

## what actually happens when you write

Your call lands in the kernel's page cache first — not on disk. The kernel marks that page dirty and a background worker flushes it to disk later on its own schedule. If you read the file immediately after writing (like this program does), the kernel just hands you the page straight from cache. No disk involved at all.

## how to run

```bash
g++ -std=c++17 -Wall -Wextra -o syscall_io syscall_io.cpp
./syscall_io
```

## requirements

Linux x86-64, any modern g++ or clang++
