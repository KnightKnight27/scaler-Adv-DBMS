# Lab 1: File Handling Using System Calls in Linux

## Aim
To perform file input and output operations using Linux system calls in C.

## Description

This experiment demonstrates the use of Linux system calls for file handling. Unlike standard library functions such as `fopen()`, `fread()`, and `fwrite()`, system calls interact directly with the OS kernel, providing low-level control over file operations.

---

## System Calls Used

| System Call | Purpose                                      |
|-------------|----------------------------------------------|
| `open()`    | Opens a file and returns a file descriptor   |
| `read()`    | Reads data from a file descriptor            |
| `write()`   | Writes data to a file descriptor             |
| `close()`   | Closes a file descriptor                     |

---

## What the Program Does

1. **Open for reading** — opens `file.txt` with `O_RDONLY`, gets a file descriptor
2. **Read** — reads up to 512 bytes into a buffer, prints contents and byte count
3. **Close** — releases the file descriptor
4. **Open for appending** — reopens with `O_WRONLY | O_APPEND`
5. **Write** — appends a new line, prints bytes written
6. **Close again** — frees system resources
7. **Error handling** — every system call return value is checked, `perror()` prints the cause on failure

---

## How to Run

```bash
# create a sample input file
echo "Hello from file.txt! This is the original content." > file.txt

# compile
gcc -o file_handling file_handling.c

# run
./file_handling
```

## Expected Output

```
File opened for reading. File descriptor: 3

--- File Contents ---
Hello from file.txt! This is the original content.

Total bytes read: 51

File closed after reading.

File opened for appending. File descriptor: 3
Total bytes written: 50
File closed after writing.
```

---

## Observations

- Every opened file gets a unique **file descriptor** (integer) from the OS — stdin=0, stdout=1, stderr=2, so the first opened file is typically 3.
- `read()` returns the actual number of bytes read, which can be less than requested.
- `O_APPEND` guarantees new data is added at the end without touching existing content.
- Always `close()` file descriptors — leaving them open leaks system resources.
- `perror()` uses the global `errno` variable to print a human-readable error message.

---

## System Calls vs Standard Library

| Feature         | System Calls (`open`, `read`) | Standard Library (`fopen`, `fread`) |
|-----------------|-------------------------------|--------------------------------------|
| Level           | Kernel level                  | User level (wraps system calls)      |
| Buffering       | No buffering                  | Buffered by default                  |
| Portability     | Linux/Unix only               | Cross-platform                       |
| Control         | More control                  | Easier to use                        |

---

## Author
Submitted as part of Lab 1 – Database Systems Lab  
Date: May 2026