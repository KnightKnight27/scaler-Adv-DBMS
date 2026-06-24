# Assignment 1

**Name:** Utkarsh Raj
**Roll no.:** 24bcs10318

---

## 1. Project Overview

This project implements a basic file input/output using raw POSIX system calls i.e. `Read`, `Write`, `Open`, `Close` and `lSeek` in c++ without using high-level libraries.

### System Calls Overview

| System Call | Purpose |
|-------------|---------|
|  `open()`   | Obtain a file descriptor for a path |
|  `write()`  | Copy data from user space into the kernel page cache |
|  `lseek()`  | Reposition the per-open-file seek offset |
|  `read()`   | Copy data from the kernel page cache into user space |
|  `close()`  | Release the file descriptor |

The program creates or truncates a file called `output.txt`, writes a string, seeks back to the beginning, reads the string again and prints it out to `stdout`.

---

## 2. Build & Run

```bash
# Compile with warnings enabled
g++ -Wall -Wextra -o file_orw file_orw.cpp

# Run normally
./file_orw

# Run under strace (to analyze the system calls)
strace -e trace=openat,open,read,write,close -o strace_output.txt ./file_orw
```

**Expected Console Output** 
```
Info: File opened successfully. File Descriptor = 3
Info: Wrote 7 bytes to the file.
Info: Seek pointer reset to offset 0.
Info: Successfully read 7 bytes.

---- File Content ----
Hello!
----------------------

Info: File Descriptor 3 closed successfully.
Completed.
```

---

## 3. The Journey of a file operation

### 🛤️ The File Journey (Under the Hood)

1. **Initialization (`open`)**
   The program calls `open()` with `O_RDWR | O_CREAT | O_TRUNC`. The OS transitions to kernel mode, allocates a new inode (or resets the file size to zero), checks permissions (`0644`), and returns a **File Descriptor** (e.g., `3`) with an initial seek offset of `0`.

2. **Writing Data (`write`)**
   The program writes `7` bytes (`"Hello!\n"`). The kernel copies this data from user space into the kernel's page cache (to be flushed to disk later). The per-open-file seek offset automatically advances by 7 bytes.

3. **Rewinding the Pointer (`lseek`)**
   To read the newly written data without closing the file, the program calls `lseek()` with `SEEK_SET`. The kernel instantly repositions the per-open-file seek offset strictly back to `0`. 

4. **Extracting Data (`read`)**
   The program calls `read()`. The kernel fetches the requested data directly from the page cache and copies those 7 bytes into the user-space `buffer`. The seek offset advances once again.

5. **Resource Cleanup (`close`)**
   The program finishes execution and calls `close()`. The kernel removes the file descriptor from the process's descriptor table, decrements the open-file reference count, and safely releases the system resources.

**Note** My initial plan was to close the fd after write was done because the internal file pointer had already moved to the end of the file during write but later I found out about `lSeek()` that helped to rewind the pointer back to 0, so the I can use the same file descriptor for writing as well as reading. This is more efficient than closing the old fd and opening a new one to read.

---

## 4. Core Concepts: Under the Hood

To fully understand how this program interacts with the operating system, it helps to grasp three key Unix concepts:

### File Descriptors (FDs)
A **File Descriptor** is a non-negative integer that the operating system uses to uniquely identify an open file within a specific process. It acts as an abstract handle or a "VIP pass" for file I/O operations.
* When a program asks the kernel to open a file, the kernel creates an entry in the system-wide "open file table" and returns an integer index (the FD). The program uses this integer in all subsequent system calls so the kernel knows exactly which file is being manipulated.
* The kernel always assigns the lowest available integer. Since `0`, `1`, and `2` are reserved for standard input, output, and error, the first file opened by a program usually gets assigned FD `3`.

### `strace` (System Call Tracer)
`strace` is a powerful Linux command-line tool used for debugging. It intercepts and records the raw system calls executed by a process.
* While high-level languages (like C++) abstract away how files are actually handled via libraries, `strace` strips away those abstractions to show you the exact conversation between your program and the OS kernel. 
* Tracing this executable reveals exactly what the kernel returned when `open` was called, the literal bytes written to the disk, the seek offset changes, and the exact moment the file descriptor was released.

### Inodes (Index Nodes)
An **Inode** is a fundamental data structure within a Unix-style file system that stores everything the operating system needs to know about a file—*except* for its human-readable name and its actual data.
* When a file is created on disk, the filesystem allocates an inode containing metadata such as file size, permissions, ownership, and timestamps.
* The most critical job of the inode is telling the OS where the actual file content lives. The inode contains a list of pointers that map directly to the physical data blocks on the hard drive.