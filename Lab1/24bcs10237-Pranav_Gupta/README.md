# File Operations using Raw System Calls (C++)

This project demonstrates how to perform file operations (Open, Read, Write, Close) using raw system calls.

## Concepts Explained

### 1. File Descriptor (FD)
A **File Descriptor** is a non-negative integer that the kernel returns to a process when it opens an existing file or creates a new one. 
- It acts as an index into the process's **File Descriptor Table**.
- Each entry in this table points to a **File Table Entry** in the kernel, which contains the current file offset, status flags, and a pointer to the file's inode.


### 2. STRACE
**Strace** is a powerful diagnostic tool used to monitor the interaction between a process and the Linux kernel.
- It intercepts and records the system calls which are called by a process and the signals which are received by a process.
- It is essential for debugging performance issues or understanding how a program interacts with the hardware.

### 3. INODE (Index Node)
An **Inode** is a data structure in a Unix-style file system that stores all the metadata about a file except its name and the actual data content.
- **What it stores**: File size, owner ID, group ID, permissions, timestamps (creation, access, modification), and most importantly, pointers to the data blocks on the disk.
- Each file in a filesystem is uniquely identified by its **Inode Number**.
- When you open a file by name, the kernel looks up the directory entry to find the inode number, then uses the inode to locate the data blocks.

## Detailed Process Flow

The lifecycle of a file operation in this program follows these steps at the kernel level:

### 1. Opening the Input File (`open`)
- The process issues the `open` system call with the filename `"input.txt"` and the `O_RDONLY` flag.
- The **Kernel** receives the request and performs an **Inode Lookup** in the filesystem to find the file's metadata and disk location.
- The Kernel checks the process's **Permissions** to ensure it has read access.
- If successful, the Kernel creates a **File Table Entry** and returns the next available **File Descriptor (FD)**.

### 2. Reading Data (`read`)
- The process calls `read` using the FD returned earlier, providing a **User-Space Buffer** and the maximum number of bytes to read.
- The Kernel uses the Inode associated with the FD to locate the physical **Data Blocks** on the storage device.
- The data is copied from the storage (or the kernel's page cache) into the process's memory buffer.
- The Kernel updates the **File Offset** in the file table entry so the next read starts where this one finished.

### 3. Creating/Opening the Output File (`open` with flags)
- The process calls `open` for `"output.txt"` with flags like `O_WRONLY`, `O_CREAT`, and `O_TRUNC`.
- **`O_CREAT`**: If the file doesn't exist, the Kernel allocates a **New Inode** and creates a directory entry.
- **`O_TRUNC`**: If the file exists, the Kernel clears its existing data blocks.
- The Kernel returns another FD (e.g., `4`).

### 4. Writing Data (`write`)
- The process calls `write` using the output FD and the data stored in its buffer.
- The Kernel copies data from the User-Space Buffer to its own internal **Write Buffer/Page Cache**.
- The Kernel eventually schedules a **Disk I/O** operation to persist this data to the physical disk blocks identified by the output file's Inode.

### 5. Closing Files (`close`)
- The process calls `close` for both FDs.
- The Kernel decrements the reference count for the file table entries.
- If the count reaches zero, the Kernel releases the FDs, making them available for future use.

## System Calls Used
- `open()`: Requests the kernel to open a file and return a File Descriptor.
- `read()`: Reads data from an FD into a buffer.
- `write()`: Writes data from a buffer to an FD.
- `close()`: Tells the kernel to release the FD and its associated resources.
