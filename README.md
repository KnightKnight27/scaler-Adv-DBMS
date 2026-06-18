# Lab 1 - Low-Level File I/O Using Linux System Calls in C++

**Author:** Shifa
**Course:** Advanced DBMS / Operating Systems
**Topic:** File I/O Using Raw Linux System Calls

---

## Overview

This project demonstrates low-level file input/output operations in Linux using direct system calls. Unlike standard C++ file handling mechanisms such as `<fstream>` or C library functions like `fopen()` and `fread()`, this implementation interacts directly with the Linux kernel through system calls.

The program reads data from an input file and writes it to an output file while illustrating how user-space applications communicate with the operating system.

---

## Objectives

* Understand Linux file system interactions.
* Learn how user-space programs invoke kernel services.
* Use raw system calls for file operations.
* Explore the path from application code to the Virtual File System (VFS).
* Demonstrate low-level file reading and writing without standard I/O libraries.

---

## System Calls Used

### `open()`

Opens a file and returns a file descriptor.

```cpp
int fd = open("input.txt", O_RDONLY);
```

---

### `read()`

Reads bytes from a file descriptor into a buffer.

```cpp
ssize_t bytesRead = read(fd, buffer, sizeof(buffer));
```

---

### `write()`

Writes bytes from a buffer to a file descriptor.

```cpp
write(fd, buffer, bytesRead);
```

---

### `close()`

Releases the file descriptor and associated kernel resources.

```cpp
close(fd);
```

---

## Project Structure

```text
Lab1/
│
├── fileio.cpp
├── input.txt
├── output.txt
└── README.md
```

---

## Working Principle

The program performs the following steps:

1. Open `input.txt` in read-only mode.
2. Open or create `output.txt` in write mode.
3. Read data from the input file using `read()`.
4. Write the data to the output file using `write()`.
5. Close both file descriptors.

Flow:

```text
User Program
      |
      v
System Call Interface
      |
      v
Virtual File System (VFS)
      |
      v
Page Cache
      |
      v
Storage Device
```

---

## Compilation

Compile using:

```bash
g++ -std=c++17 -Wall -Wextra -o fileio fileio.cpp
```

---

## Execution

Run the program:

```bash
./fileio
```

---

## Sample Input

Contents of `input.txt`:

```text
Hello Linux System Calls
Learning Low-Level File I/O
```

---

## Sample Output

Contents of `output.txt` after execution:

```text
Hello Linux System Calls
Learning Low-Level File I/O
```

---

## Educational Concepts Demonstrated

* Linux System Calls
* File Descriptors
* User Space vs Kernel Space
* Virtual File System (VFS)
* Page Cache
* Low-Level File Operations
* Operating System Resource Management

---

## Advantages of System Call Based I/O

* Greater control over file operations.
* Closer understanding of operating system internals.
* Useful for systems programming and kernel-level concepts.
* Minimal abstraction overhead.

---

## Conclusion

This lab demonstrates how file operations are performed at the operating system level using Linux system calls. By bypassing high-level C and C++ I/O libraries, the implementation provides insight into the interaction between user-space applications and the kernel, including file descriptors, VFS, and storage management mechanisms.
