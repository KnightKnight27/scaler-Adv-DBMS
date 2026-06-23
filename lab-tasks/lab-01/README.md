# Lab 1: C++ File I/O Traced with Strace

## Objective
To trace and understand the journey of data from a user-space C++ application down to the physical disk by observing system calls (syscalls). We trace the flow: User Space -> VFS (Virtual File System) -> Page Cache -> Physical Disk.

## Running the Trace

To observe the system calls, compile the C++ program and run it using `strace`:

```bash
g++ main.cpp -o file_io
strace -e trace=openat,write,fsync,read,close ./file_io
```

## Expected `strace` Output & Explanation

When the program executes, `strace` intercepts the system calls. Here is the breakdown of the journey:

### 1. `openat()`
```c
openat(AT_FDCWD, "test_io.txt", O_WRONLY|O_CREAT|O_TRUNC, 0600) = 3
```
* **What happens:** The application requests a file descriptor. The Virtual File System (VFS) checks if the file exists. It allocates an **inode** (index node) on the file system and returns an integer (e.g., `3`), which is the file descriptor mapping to this inode in the process's file descriptor table.

### 2. `write()`
```c
write(3, "Advanced DBMS Lab 1: System Cal"..., 42) = 42
```
* **What happens:** The `write()` system call is issued. 
* **Crucial detail:** This does *not* write to the physical hard drive immediately. Instead, VFS copies the buffer into the kernel's **Page Cache**. The page in memory is marked as "dirty". This makes writes extremely fast since they are just memory copies.

### 3. `fsync()`
```c
fsync(3) = 0
```
* **What happens:** The application explicitly requests that the dirty pages associated with file descriptor `3` in the Page Cache be flushed to the physical storage device immediately.
* **Why database systems need this:** Databases (like PostgreSQL's WAL) rely on `fsync()` to guarantee durability (the 'D' in ACID). If the power fails before `fsync` completes, the data might only exist in the volatile Page Cache and would be lost.

### 4. `read()`
```c
read(3, "Advanced DBMS Lab 1: System Cal"..., 99) = 42
```
* **What happens:** We closed and re-opened the file, then called `read()`. Because we recently wrote to this file, the operating system still holds this file's blocks in the **Page Cache**.
* **Result:** The VFS intercepts the read request, finds a cache hit in the Page Cache, and serves the data directly from RAM without having to perform a slow block I/O operation from the physical disk.

## Summary of the Journey
1. **Inode lookup/creation:** Handled by `open()`.
2. **User-Space to Kernel-Space (Page Cache):** Handled by `write()`. Memory is marked dirty.
3. **Page Cache to Physical Disk:** Enforced by `fsync()`.
4. **Physical Disk (or Page Cache) to User-Space:** Handled by `read()`.
