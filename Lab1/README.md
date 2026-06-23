# Lab 1: C++ File I/O and Kernel Trace

In this lab, we explore what happens when a C++ program opens and reads a file. We'll trace the process from the syscall level down to the kernel's virtual filesystem (VFS) and inodes.

## 1. Simple C++ File Reader
First, we wrote a basic C++ program to read from a file. The code is available in `reader.cpp`.

To compile and set up the test environment:
```bash
echo "hello from lab 1" > test.txt
g++ -o reader reader.cpp
```

## 2. Tracing syscalls using strace
We used `strace` to observe the system calls made by the compiled program:
```bash
strace -e trace=openat,read,close,fstat,mmap ./reader
```

Some of the main system calls we noticed:
- `openat`: Opens the file and gives us a file descriptor (fd).
- `fstat`: Retrieves metadata for the fd, such as size, timestamps, and permissions.
- `read`: Reads the actual bytes from the file into memory.
- `mmap`: Maps the C++ runtime and shared libraries into the process's memory.
- `close`: Closes the fd and releases resources.

A simplified output of the trace looks like this:
```
openat(AT_FDCWD, "test.txt", O_RDONLY) = 3
fstat(3, {st_mode=S_IFREG|0644, st_size=18, ...}) = 0
read(3, "hello from lab 1\n", 4096)    = 18
read(3, "", 4096)                       = 0
close(3)                                = 0
```

## 3. How the kernel handles the inode
When `openat` is executed, the kernel performs several steps:
1. Path resolution: It traverses the directory tree starting from the current working directory.
2. Inode lookup: It finds the inode corresponding to the filename via the VFS layer.
3. Permission check: It verifies if the current process has the required access rights based on the inode's properties.
4. File descriptor allocation: An fd is allocated pointing to a `file` struct that keeps track of the file offset and points back to the inode.

To view the inode number:
```bash
ls -i test.txt
stat test.txt
```

## 4. The kernel stack
The layers involved when reading a file in C++:
C++ std::ifstream -> libc -> read() syscall -> VFS -> Filesystem driver -> Page cache -> Block device driver.

If the file was recently accessed, the data might be served directly from RAM (page cache) without hitting the disk. Also, getting file metadata via `fstat` is very fast because the kernel caches it.

## 5. Checking open files in /proc
While the process is running, you can inspect its open file descriptors:
```bash
ls -l /proc/<PID>/fd
stat /proc/<PID>/fd/3
```

## Conclusion
- Opening a file in C++ eventually triggers an `openat` syscall, finding the corresponding inode via the VFS.
- The file descriptor is local to the process, whereas the inode is managed by the kernel.
- Using `strace` helps us see the exact boundary where our user-space code talks to the kernel.
- Repeated reads benefit from the page cache, preventing unnecessary disk I/O.
