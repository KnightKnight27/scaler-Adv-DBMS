# Lab 1 - Raw File Operations Using System Calls

**Name:** Srujan Gowda KS
**Roll Number:** 24BCS10339

## Objective

The objective of this lab was to understand how file operations work at the operating system level using raw system calls instead of C++ file handling libraries. The program uses `open()`, `read()`, `write()`, and `close()` to perform file operations directly through the kernel.

---

## Program Description

The program performs the following tasks:

1. Creates and opens `input.txt`.
2. Writes sample text into the file.
3. Closes the file.
4. Opens the file again in read mode.
5. Reads the contents of the file.
6. Creates `output.txt`.
7. Copies the contents from `input.txt` to `output.txt`.
8. Closes all opened file descriptors.

---

## What I Learned

### File Descriptors

A file descriptor (FD) is a small integer returned by the operating system when a file is opened. It acts as a handle that allows the program to access the file.

Common file descriptors are:

* `0` → Standard Input
* `1` → Standard Output
* `2` → Standard Error

Any file opened by the program usually gets descriptor `3` or higher.

---

### System Calls

The lab introduced me to four important system calls:

#### open()

Used to open or create a file and obtain a file descriptor.

```cpp
int fd = open("input.txt", O_RDONLY);
```

#### read()

Reads data from a file into a buffer.

```cpp
read(fd, buffer, sizeof(buffer));
```

#### write()

Writes data from memory into a file.

```cpp
write(fd, buffer, length);
```

#### close()

Releases the file descriptor and associated resources.

```cpp
close(fd);
```

---

### Understanding strace

I learned about the `strace` utility, which helps monitor system calls made by a program.

Example:

```bash
strace ./a.out
```

Using strace, I could observe system calls such as:

* open()
* read()
* write()
* close()

This helped me understand how user programs communicate with the Linux kernel.

---

### Understanding Inodes

An inode is a data structure used by Linux to store information about a file.

It contains:

* File size
* Permissions
* Owner details
* Timestamps
* Storage block information

The inode stores metadata but not the filename itself. Filenames are stored in directory entries that point to inodes.

---

## Additional Topics Studied

### Pages

Memory is managed in fixed-size blocks called pages, usually 4 KB in size. File data is often loaded into memory pages to improve performance.

### Drivers

Drivers are software components that allow the operating system to communicate with hardware devices such as storage drives.

### DMA (Direct Memory Access)

DMA allows hardware devices to transfer data directly to RAM without requiring the CPU to copy every byte manually, improving performance.

### Eviction

Since memory is limited, the operating system removes less frequently used pages when space is needed. This process is called eviction.

---

## My Understanding of the File Journey

When a file is read:

1. The program calls `open()`.
2. The kernel checks permissions and locates the file.
3. A file descriptor is returned.
4. `read()` requests data from the file.
5. The kernel retrieves data from memory or storage.
6. Data is copied into the program buffer.
7. `close()` releases the file descriptor.

Similarly, when data is written, the kernel receives data through `write()` and eventually stores it on disk.

---

## Conclusion

This lab helped me understand the difference between high-level file handling and low-level system calls. I learned how file descriptors work, how Linux manages files using inodes, and how concepts such as pages, drivers, DMA, and eviction contribute to file operations. The assignment gave me a better understanding of what happens behind the scenes whenever a file is opened, read, written, or closed.
