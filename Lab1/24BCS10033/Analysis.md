# Lab 1: C++ File I/O Traced with Strace (System Call Journey)

## 1. System Call Tracing (Strace Output)

When executing our C++ file I/O program, we can trace the system calls made by the process. Below is the parsed `strace` output of the key operations in our program:

```strace
// 1. Opening the file
openat(AT_FDCWD, "sample_io_test.txt", O_WRONLY|O_CREAT|O_TRUNC, 0600) = 3

// 2. Writing the buffer (162 bytes) to the file descriptor 3
write(3, "Advanced DBMS Lab 1: Low-level f"..., 162) = 162

// 3. Forcing disk synchronization
fsync(3)                                = 0

// 4. Closing the file descriptor
close(3)                                = 0
```

---

## 2. The Journey of a Write: Inode $\rightarrow$ VFS $\rightarrow$ Page Cache $\rightarrow$ Syscall

When the C++ code invokes the write operation, the data travels through several layers of the Operating System kernel before arriving at the physical storage media. Here is the step-by-step trace of this journey:

```
+--------------------------------------------------------+
|                   User-Space Buffer                    |
|  (data.c_str() resides in application virtual memory)  |
+--------------------------+-----------------------------+
                           |
                           v  [System Call Transition]
+--------------------------------------------------------+
|                  VFS (Virtual File System)             |
|   (sys_write() resolves fd=3 to file inode structures) |
+--------------------------+-----------------------------+
                           |
                           v  [Buffer Memory Copy]
+--------------------------------------------------------+
|             Page Cache (Memory Page Frames)            |
|  (Data copied to kernel memory page; marked as DIRTY)   |
+--------------------------+-----------------------------+
                           |
                           v  [Checkpointer / fsync / pdflush]
+--------------------------------------------------------+
|           Block Device Driver (I/O Scheduler)          |
| (Translates logical file blocks into physical sectors)  |
+--------------------------+-----------------------------+
                           |
                           v  [SATA/NVMe Command Flush]
+--------------------------------------------------------+
|               Physical Storage Disk Device             |
|          (Persistent flash blocks or HDD platter)      |
+--------------------------------------------------------+
```

### Step 1: User-Space Buffer to System Call (`write`)
- The C++ program holds the data string in virtual memory allocated to the user process. 
- When `write(fd, buf, size)` is called, the CPU transitions from user mode to kernel mode via an interrupt or assembly instruction (`syscall`).
- The kernel copies the arguments and invokes the handler function `sys_write()`.

### Step 2: Virtual File System (VFS) Routing
- The VFS layer manages the generic file interface. It maps the integer file descriptor `3` to the process's file descriptor table, retrieving the corresponding `file` struct.
- The `file` struct points to an **Inode** structure representing the file on the underlying filesystem (e.g., ext4, APFS).
- VFS routes the write request to the filesystem-specific write operation (e.g., `ext4_file_write_iter`).

### Step 3: Page Cache Allocation and Dirtying
- Operating systems do not perform immediate disk writes because disk I/O is slow. Instead, the filesystem writes the data to the **Page Cache** (in-memory caching layer of 4 KB page frames).
- The kernel locates the memory page corresponding to the target offset in the file.
- The data is copied from the user-space buffer into this kernel cache page.
- The page is marked with a **DIRTY flag** (specifying that the in-memory page is newer than the copy on disk) and added to the kernel's dirty page list.
- At this point, the `write` system call returns control to the user application. The write is complete from the application's perspective, but the data is not yet physically durable on disk!

### Step 4: Flushes and Inode Block Allocation
- To make the data durable, the kernel must write the dirty pages back to disk. This occurs:
  1. Periodically via background writeback threads (e.g., `pdflush`, `flush`, or `kswapd`).
  2. When dirty memory limits are exceeded.
  3. When forced by the application via the **`fsync()`** system call.
- During writeback, the filesystem driver (e.g., ext4) requests physical block allocation for the file.
- It queries the file's **Inode** structure to map logical file offsets to physical block numbers on the storage device. If the file is growing, the filesystem allocates new blocks using block allocators (like extents) and updates the inode's block index list.

### Step 5: Device Driver and Physical Disk Write
- The filesystem submits write I/O requests (in the form of bio structs) to the block device layer.
- The I/O scheduler queues, merges, and schedules these blocks, passing them to the physical device driver (e.g., NVMe or SATA driver).
- The driver issues write commands to the disk controller, which writes the bytes onto the non-volatile storage media (NAND flash blocks or magnetic sectors).
- Once the drive confirms execution, the kernel marks the cached page as **CLEAN** and the `fsync()` syscall returns successfully.
