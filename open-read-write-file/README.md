# Exploring POSIX File I/O System Calls

This repository contains a set of minimal C++ programs demonstrating raw POSIX file I/O operations (`open`, `read`, `write`, `close`). It intentionally avoids standard library abstractions like `std::fstream` or `FILE*` to expose the underlying system-level mechanics.

## Motivation: Dropping Down to the Kernel

While standard library wrappers offer convenience and buffering, operating system kernels exclusively handle I/O via file descriptors and raw byte buffers. By bypassing these high-level interfaces, these examples highlight critical low-level concepts: file descriptors, signal interruptions (`EINTR`), partial data reads/writes, and error handling via `errno`.

Key headers: 
- `<fcntl.h>`: File control options (`open` and flags).
- `<unistd.h>`: Standard symbolic constants and types (`read`, `write`, `close`, `sleep`).

## Programs Overview

### 1. File Descriptors in Action (`open_demo.cpp`)
This program opens a target file in read-only mode (`O_RDONLY`), displays the resulting file descriptor and the process ID, and then pauses execution for 30 seconds.

**Usage:**
```bash
./open_demo filename.txt
# In a separate terminal window, inspect the process's open files:
lsof -p <pid>
```
*Purpose:* Allows real-time observation of the file descriptor populated in the kernel's file table for the active process.

### 2. Implementing a Basic `cat` (`read_demo.cpp`)
A straightforward utility that opens a file and echoes its contents to standard output using a 4KB buffer loop consisting of `read()` and `write()` calls.

**Features:**
- Detects End-Of-File (EOF) when `read() == 0`.
- Retries automatically on interrupt signals (`EINTR`).
- Manages partial writes by continuously looping until the buffer is fully flushed.

**Usage:**
```bash
./read_demo filename.txt
```

### 3. Writing to Files (`write_demo.cpp`)
Demonstrates file creation and writing by opening a file with flags `O_WRONLY | O_CREAT | O_TRUNC` and permissions `0644`. It writes a user-provided string to the file, correctly handling any partial write scenarios.

**Usage:**
```bash
./write_demo output.txt "Your text here"
```

## Compilation

You can build the executables using `g++`:

```bash
g++ -Wall -o open_demo  open_demo.cpp
g++ -Wall -o read_demo  read_demo.cpp
g++ -Wall -o write_demo write_demo.cpp
```

## Key Learnings

- The `open()` syscall returns an integer file descriptor (fd), or `-1` on failure (with `errno` populated).
- Syscalls like `read()` and `write()` may process fewer bytes than requested, necessitating loop constructs for complete execution.
- Resource cleanup is mandatory: always `close()` file descriptors.
- Error validation is crucial: verify return values and decode failures using `strerror(errno)`.
- When using `O_CREAT`, a permission mode (e.g., `0644`) is required. The `O_TRUNC` flag will erase existing contents.