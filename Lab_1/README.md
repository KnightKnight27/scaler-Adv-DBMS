<div align="center">

# 📁 Linux File Handling Using System Calls
### C Programming with Low-Level Kernel Interfaces

[![C](https://img.shields.io/badge/C-00599C?style=for-the-badge&logo=c&logoColor=white)](https://en.cppreference.com/w/c)
[![Linux](https://img.shields.io/badge/Linux-FCC624?style=for-the-badge&logo=linux&logoColor=black)](https://www.kernel.org/)

</div>

---

## 👨‍🎓 Student Details
- **Name:** Siddhant Prasad
- **Roll Number:** 24BCS10255

---

## 🎯 Aim
To perform file input and output operations using low-level Linux system calls in C.

---

## 🔍 Description
This experiment demonstrates the use of Linux system calls for file handling. Unlike standard C library functions such as `fopen()`, `fread()`, and `fwrite()`, system calls interact directly with the operating system kernel. This provides low-level control over file operations, memory allocation, and OS-specific performance tuning.

### Program Workflow
1. **Setup**: If `file.txt` does not exist, the program pre-creates it using `open()` with `O_CREAT` and writes some initial test data to it.
2. **Open File (Read Mode)**: Opens `file.txt` in read-only mode (`O_RDONLY`), obtaining a unique non-negative integer known as a **File Descriptor (fd)** from the operating system.
3. **Read File**: Reads up to `512` bytes from the file descriptor into a local memory buffer and displays the contents on the terminal alongside the count of bytes successfully read.
4. **Close File**: Closes the file descriptor to free up kernel resources.
5. **Open File (Append Mode)**: Reopens `file.txt` in write-only and append mode (`O_WRONLY | O_APPEND`), ensuring existing file contents are not overwritten.
6. **Write File**: Appends a completion message to the file and displays the count of written bytes.
7. **Close File**: Closes the file descriptor again for proper system cleanup.
8. **Error Handling**: Monitors the return values of all system calls and reports errors using `perror()` if they occur (returning `-1` is typical of a system call failure).

---

## ⚙️ System Calls Used

| System Call | Purpose | Key Arguments | Return Value (Success / Fail) |
| :--- | :--- | :--- | :--- |
| `open()` | Opens/creates a file | `path`, `flags` (e.g., `O_RDONLY`, `O_WRONLY`, `O_APPEND`, `O_CREAT`), `mode` | File Descriptor `(>= 0)` / `-1` |
| `read()` | Reads bytes from a file descriptor | `fd`, `buf`, `count` | Bytes read `(> 0)`, EOF `(0)` / `-1` |
| `write()` | Writes bytes to a file descriptor | `fd`, `buf`, `count` | Bytes written `(>= 0)` / `-1` |
| `close()` | Closes a file descriptor | `fd` | `0` / `-1` |

---

## 🛠️ Compilation and Execution

### Prerequisites
A Linux environment (or Windows Subsystem for Linux - WSL) with `gcc` installed.

### Build and Run
Compile the code using `gcc`:
```bash
gcc -Wall file_syscalls.c -o file_syscalls
```

Execute the binary:
```bash
./file_syscalls
```

---

## 📝 Key Observations
- **File Descriptors**: Every open file is tracked by the kernel with a unique integer index (e.g., `3` or `4`, since `0` is stdin, `1` is stdout, and `2` is stderr).
- **Direct Kernel Mode**: File buffers are directly copied from disk block caches to user-space buffers without passing through standard I/O library caches (`stdio.h`'s internal buffering).
- **Safety**: Robust check on system call returns (checking for `-1`) prevents program execution from continuing with stale or invalid file descriptors, which could crash the shell or cause memory leaks.

---

## 🏁 Conclusion
Through this lab, we successfully demonstrated low-level file I/O operations using Linux system calls. We established a firm understanding of file descriptors, kernel-level file operations, robust error checking using `perror`, and the performance differences between low-level system calls (`open`, `read`, `write`, `close`) and their buffered standard library counterparts.
