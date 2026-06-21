# Lab Session 1: File I/O in C++ — Kernel Journey via strace

## Objective
Understand what happens under the hood when C++ opens and reads a file: from the syscall layer down to inodes, the VFS, and kernel interactions.

---

## Steps

### 1. Write a simple C++ file reader
The environment contains `reader.cpp` and a target text file `test.txt` generated using:

```bash
echo "hello from lab 1" > test.txt
g++ -o reader reader.cpp
```
### 2. Trace with strace
Running the compiled binary through strace filtering for file and memory management system calls yields the following trace boundary:

```bash
strace -e trace=openat,read,close,fstat,mmap ./reader
```
## Key syscalls observed:

- **openat**: Opens the target file and returns a non-negative integer representing the file descriptor (fd).

- **fstat**: Fetches the inode metadata (such as size, permissions, and hard link count) associated with that file descriptor.

- **read**: Copies a specified number of bytes from the file into a user-space buffer structure.

- **mmap**: Maps the necessary C++ runtime environment and shared libraries into the process's virtual memory address space.

- **close**: Releases the file descriptor and decrements the kernel-level inode reference count.

## Terminal Trace Output:

- **openat(AT_FDCWD, "test.txt", O_RDONLY)** = 3  
  → File opened successfully, file descriptor 3 returned.

- **fstat(3, {...})** = 0  
  → Retrieved file metadata (size = 17 bytes, permissions = 0644).

- **read(3, "hello from lab 1\n", 4096)** = 17  
  → Read 17 bytes from file into buffer.

- **read(3, "", 4096)** = 0  
  → End of file reached (EOF).

- **close(3)**  
  → File descriptor released and kernel resources freed.

  ### 3. The inode journey

When the `openat` system call executes, the Linux kernel handles the transition through these defined operations:

- **Path resolution**: Walks the directory structure starting from the current working directory (`AT_FDCWD`) to resolve components sequentially.

- **Inode lookup**: Translates the directory entry string mapping (`test.txt`) to its underlying inode number via the Virtual Filesystem (VFS) layer.

- **Permission check**: Evaluates the running process's UID/GID attributes against the inode's specific permission bits (`st_mode`).

- **File descriptor allocation**: Reserves an active slot inside the process's open-file descriptor table pointing to an internal `struct file` containing file offsets.

- **Inspecting file inode properties**:
```bash
ls -i test.txt
```
2847139 test.txt
```bash
stat test.txt
```

# File Metadata (stat command output)

* **File**: test.txt  
* **Size**: 17 bytes  
* **Blocks**: 8  
* **IO Block**: 4096  
* **Type**: regular file  

* **Device**: 802h/2050d  
* **Inode**: 2847139  
* **Links**: 1  

* **Access Permissions**: (0644/-rw-r--r--)  
* **User ID (Uid)**: (1000/user)  
* **Group ID (Gid)**: (1000/user)  

* **Last Access Time**: 2026-06-21 15:12:04.118941002 +0530  
* **Last Modification Time**: 2026-06-21 15:12:04.118941002 +0530  
* **Last Change Time**: 2026-06-21 15:12:04.118941002 +0530  
* **Birth Time**: -

   ### 4. Kernel layers involved
```   
   C++ std::ifstream
      |
   fread / libc
      |
   read() syscall  <-- User/Kernel Space Boundary
      |
   VFS (Virtual Filesystem Switch)
      |
   Filesystem driver (ext4 / btrfs)
      |
   Page cache (Serving data directly from RAM if cached)
      |
   Block device driver → Physical Storage Media (On cache miss)
 ```  
 * **Page Cache**: Subsequent executions or reads of the exact same blocks bypass physical disk I/O operations entirely, retrieving data directly from kernel memory space.

* **Metadata Performance**: `fstat` queries execute with minimal overhead because file metadata caches directly within the kernel’s active inode cache structure (`icache`).

 ### 5. Verify with /proc filesystem
   During active execution runtime loops (e.g., PID 14285), file descriptors can be validated directly out of the process subsystem:


   ```bash
     echo "hello from lab 1" > test.txt
     g++ -o reader reader.cpp
```
```
total 0
lrwx------ 1 user user 64 Jun 21 15:13 0 -> /dev/pts/0
lrwx------ 1 user user 64 Jun 21 15:13 1 -> /dev/pts/0
lrwx------ 1 user user 64 Jun 21 15:13 2 -> /dev/pts/0
lr-x------ 1 user user 64 Jun 21 15:13 3 -> /home/user/labs/test.txt

```
Checking specific descriptor pointers references back directly to the file node target:

   ```bash
     stat /proc/14285/fd/3
```

```
File: /proc/14285/fd/3 -> /home/user/labs/test.txt
  Size: 17        	Blocks: 8          IO Block: 4096   regular file
Device: 802h/2050d	Inode: 2847139     Links: 1
```
# Key Takeaways

* Every instantiation of `std::ifstream` interfaces down to native `openat` calls traversing through VFS translation rules.

* File descriptors act strictly as per-process integer handles; the inode serves as the kernel's definitive representation of the underlying data.

* Utilizing `strace` exposes the definitive execution boundary separating user-space application runtimes from low-level kernel execution rings.