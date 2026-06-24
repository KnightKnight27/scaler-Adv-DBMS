# Advanced DBMS Lab 1: System Call and Storage Path Analysis

**Name:** Om Malviya  
**Roll Number:** 24BCS10448  
**Course:** Advanced Database Management Systems  

---

## Overview

When we use standard C++ file streams like `std::ofstream`, the runtime environment completely hides how the operating system interacts with the physical hardware. The objective of this lab is to bypass those high-level wrappers and use raw POSIX system calls to trace the exact path a piece of data takes from application memory down to the underlying storage drive.

### Program Workflow
The provided C++ implementation executes a complete write-and-read cycle:
1. Opens a file named `assignment-data.txt`.
2. Writes a multi-line text payload directly to the kernel using `write()`.
3. Enforces physical durability by flushing the data to disk using `fsync()`.
4. Closes the file, reopens it in read-only mode, reads the bytes back into user memory, and outputs them to the console.

---

## The End-to-End Data Journey

When the application runs, the data transitions through five core operating system layers:

### 1. Crossing the System Call Boundary
Programs running in user space do not have the necessary CPU privileges to interact with hardware. When our code invokes `open()`, `write()`, or `read()`, it triggers a software interrupt (or `syscall` instruction). This causes a context switch where the CPU switches from user mode to kernel mode, allowing the operating system to safely execute the I/O operation on our behalf.

### 2. Path Resolution via VFS and Inodes
When we pass the filename `"assignment-data.txt"` to `open()`, the Virtual File System (VFS) handles the path resolution. It inspects the filesystem's directory entries to find the corresponding **Inode**—a metadata record that tracks the file's physical disk blocks, file size, and access permissions. Once verified, the kernel assigns our process a small integer called a **file descriptor** (in our trace, `3`). The application uses this handle for all subsequent operations.

### 3. Buffering in the Page Cache
Calling `write()` does not immediately write data to the SSD. To optimize performance, the kernel copies our text payload into an internal area of RAM called the **Page Cache** and marks those memory pages as "dirty" (modified in memory, but not yet synced to disk). Because writing to RAM is extremely fast, `write()` returns success almost instantly. However, if the machine were to suddenly lose power at this exact moment, the data would be lost.

### 4. Guaranteeing Persistence with `fsync`
To ensure that our data actually survives a system crash or power failure, we explicitly invoke `fsync()`. This system call blocks application execution while the kernel instructs the storage device controller to flush all dirty pages associated with our file's Inode out of RAM and onto the physical non-volatile storage media. Once the hardware acknowledges the write, `fsync()` returns control to our program.

### 5. Efficient Reads and DMA
When the program immediately reopens the file and executes a `read()`, the kernel checks its Page Cache first. Because we just wrote the file, the text pages are still cached in RAM. The kernel serves the request directly from memory resulting in a cache hit, meaning zero physical disk I/O is required. 

*(Note: If the data had not been in RAM, the storage controller would have utilized **Direct Memory Access (DMA)** to transfer the blocks from the disk directly into the Page Cache without forcing the CPU to copy the data byte-by-byte).*

---

## Connection to Database Architecture

Understanding this exact POSIX I/O lifecycle is crucial for database engineering, as enterprise storage engines (like InnoDB or PostgreSQL) are built around managing these specific OS mechanics:

* **Buffer Pools vs. Page Cache:** Because general-purpose OS page caching isn't optimized for complex B-Tree lookups, database engines typically open files with the `O_DIRECT` flag. This completely bypasses the kernel's Page Cache, allowing the database to manage its own dedicated in-memory buffer pool with customized eviction policies.
* **Write-Ahead Logging (WAL):** To guarantee ACID durability without crippling transaction speeds, databases do not immediately write every full table update to disk. Instead, they append small records of the changes to a sequential Write-Ahead Log file and call `fsync()`. Once the log is safely synced, the transaction is marked as committed, allowing the larger database tables to be lazily flushed to disk later in the background.

---

## System Call Trace Analysis

Executing the compiled binary under system call tracing (`strace -e trace=openat,write,fsync,read,close ./file_io`) captures the raw kernel interactions:

```text
openat(AT_FDCWD, "assignment-data.txt", O_WRONLY|O_CREAT|O_TRUNC, 0666) = 3
write(3, "Advanced DBMS Lab 1\nThis file wa"..., 178) = 178
fsync(3)                                = 0
close(3)                                = 0

openat(AT_FDCWD, "assignment-data.txt", O_RDONLY) = 3
read(3, "Advanced DBMS Lab 1\nThis file wa"..., 2047) = 178
close(3)                                = 0
```

* **`= 3`**: The kernel successfully allocated file descriptor `3` (`0`, `1`, and `2` are automatically reserved for stdin, stdout, and stderr).
* **`178`**: Exactly 178 bytes were transferred from user space to kernel space during the write, and successfully retrieved during the read.
* **`= 0`**: The `fsync` and `close` system calls returned zero, indicating success with no underlying hardware faults or permission errors.