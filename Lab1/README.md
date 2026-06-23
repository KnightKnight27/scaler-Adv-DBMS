# Lab 1: C++ File I/O Traced with strace
**Roll No:** 24BCS10478  
**Student Name:** Manthan Kalra  

---

## 1. Overview & Objective
The goal of this lab is to explore the journey of a write operation from user space down to physical storage, understanding the role of System Calls, the Virtual File System (VFS), the Linux Page Cache, and Inodes.

We trace the operations using `strace` to contrast standard C++ buffered I/O (`std::fstream`) with low-level POSIX System Calls (`write`, `read`).

---

## 2. Inode → VFS → Page Cache → Syscall Journey

When your C++ program writes data to a file, the data traverses through the following layers:

```
[ User Application (C++ main.cpp) ]
                │
                ▼  (e.g., standard library fstream buffer in user space)
[ std::streambuf Buffer ]
                │
                ▼  (Invokes write() system call when flushed or closed)
   [ Syscall Interface (sys_write) ]
                │
                ▼  (Translates descriptor to virtual file object)
   [ Virtual File System (VFS) ]
                │
                ▼  (Locates page; copies user buffer into kernel page)
     [ Page Cache (Dirty Page) ]
                │
                ▼  (pdflush / kswapd / fsync triggers writeback)
 [ File System (ext4 / inode lookup) ] 
                │
                ▼  (Block mapping & layout allocation)
       [ Block I/O Layer ]
                │
                ▼  (DMA transfer)
       [ Physical Disk ]
```

### Detailed Lifecycle of a Write:
1. **User Space Buffering**: When using C++ `std::ofstream`, the data is first placed in a user-space buffer (`std::streambuf`). No system call is executed immediately.
2. **System Call execution**: When the stream buffer is filled, explicitly flushed (`std::flush`), or closed, the C++ runtime issues the `write()` system call.
3. **Virtual File System (VFS)**: The kernel receives the `write(fd, buffer, count)` system call. The VFS layer maps the integer file descriptor `fd` to the kernel's internal `struct file`.
4. **Inode Resolution**: The file's inode contains metadata (permissions, file size, block addresses). VFS uses the inode to identify where the data should reside and verify write permissions.
5. **Page Cache Interaction**: The kernel writes the data to the **Page Cache** (pages of physical RAM caching disk blocks). The corresponding page is marked as **dirty**. The syscall returns `success` to the user program immediately (write is asynchronous by default).
6. **Flushing/Writeback**: The kernel thread (`flusher` or `pdflush`) periodically wakes up and writes dirty pages back to the block device, or an explicit system call like `fsync()` forces an immediate writeback.

---

## 3. strace Analysis

Running the compiled binary with `strace` exposes the system calls executed by the kernel.

### Compiling and Running
```bash
g++ -std=c++17 -o main main.cpp
strace -o strace_output.txt ./main
```

### Key Traced Observations

#### 1. C++ Buffered `fstream` Write Path:
In C++, a call to `outFile << data` does not write directly. Instead, when `outFile.close()` is called:
```strace
openat(AT_FDCWD, "fstream_test.txt", O_WRONLY|O_CREAT|O_TRUNC, 0666) = 3
write(3, "Hello, this is a C++ fstream buffered write journey!\n", 53) = 53
close(3) = 0
```
*Note: A single `write` call is issued when the stream is flushed or closed, minimizing context switches.*

#### 2. POSIX Low-level Path:
Direct POSIX call writes immediately:
```strace
openat(AT_FDCWD, "syscall_test.txt", O_WRONLY|O_CREAT|O_TRUNC, 0600) = 3
write(3, "Hello, this is a POSIX low-level write system call journey!\n", 60) = 60
close(3) = 0
```

---

## 4. Key Differences: Buffered vs. Unbuffered I/O

| Feature | C++ fstream / std::ofstream | POSIX System Calls (`write`) |
|---|---|---|
| **User-Space Buffer** | Yes (`std::streambuf`) | No (Direct copy to Page Cache) |
| **System Call Frequency**| Low (only on flush/close/overflow) | High (every invocation of `write`) |
| **Context Switch Overhead**| Minimized | High |
| **Ideal For** | Sequential, small writes | Large chunk-based, database page writes |