# Lab 1: Raw File I/O with System Calls

**Roll Number:** `24BCS10505`  
**Name:** `Vanditabyaa Dwivedi`  

## Overview
This project demonstrates reading from and writing to a file in C++ using **only raw system calls** (`open`, `read`, `write`, `close`), deliberately avoiding high-level standard libraries like `<iostream>` or `<fstream>`.

## Operations Journey

The execution in `main.cpp` follows a precise sequence of low-level operations:

### 1. Opening the File for Reading
- The program uses the `open()` system call to open `file.txt` with the `O_RDONLY` (Read Only) flag.
- It checks whether the returned file descriptor (`fd`) is valid (i.e., `>= 0`). If the file does not exist or permissions are denied, the program logs an error utilizing `errno` and exits gracefully.

### 2. Reading from the File
- The program initializes a `char buff[512]` buffer to process the file in manageable chunks.
- A `while` loop repeatedly invokes the `read()` system call to pull data from the file descriptor into the buffer. This loop continues until `read()` returns `0` (indicating End-Of-File).
- It tracks the accumulated data size in `total_read` to report the total bytes processed.

### 3. Writing to Standard Output
- Instead of relying on standard I/O library functions like `cout` or `printf` for the file content, the program uses the `write()` system call targeting `STDOUT_FILENO` (file descriptor `1`).
- To ensure data integrity and safeguard against partial writes, a nested loop runs until the exact number of bytes loaded into the buffer are completely written to the terminal.

### 4. Closing the File
- After reading the entire file, the `close()` system call is executed. This is a crucial step to release the file descriptor resource back to the operating system and prevent file handle leaks.

### 5. Opening the File for Writing
- The program opens `file.txt` a second time, now providing a combination of flags: `O_WRONLY | O_APPEND | O_CREAT`.
  - `O_WRONLY`: Ensures write-only access.
  - `O_APPEND`: Appends any new data to the very end of the file rather than overwriting existing text.
  - `O_CREAT`: Ensures the file is created with `0644` permissions if it was accidentally deleted between the read and write operations.

### 6. Writing to the File
- A static string `"abcd\n"` is prepared for writing.
- The `write()` system call is invoked to append this text directly into the file.
- As with the terminal output, a `while` loop safeguards against partial writes by verifying that the total bytes written matches the intended string length.

### 7. Final Cleanup
- Finally, the `close()` system call is invoked a second time, cleanly terminating the I/O operations before exiting the program.

## System Calls Used

| System Call | Purpose |
|---|---|
| `open()` | Open file and obtain file descriptor |
| `read()` | Read bytes from file into memory buffer |
| `write()` | Write bytes to terminal/file |
| `close()` | Release file descriptor |

## Compilation and Execution

1. **Compile the code:**
   ```bash
   g++ main.cpp
   ```

2. **Execute the compiled program:**
   ```bash
   ./a.out
   ```
