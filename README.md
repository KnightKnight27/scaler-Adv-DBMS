# Kernel File I/O Assignment

## How to run
```sh
g++ file_io.cpp -o file_io
./file_io
```

## System Call Journey
When we run this code, it does not use standard libraries (like `fstream` or `stdio`). We are talking directly to the kernel using raw system calls.
1. `open()`: The program asks the OS to open `testfile.txt`. The CPU switches from user mode to kernel mode. The OS checks if we have permission, sets up the file in memory, and returns an integer (File Descriptor).
2. `write()`: We tell the OS to take data from our variable and write it to the file descriptor. The kernel copies data to its own buffer and eventually writes it to the hard drive.
3. `close()`: Tells the OS to free up the resources and close our connection to the file.
4. `read()`: We ask the OS to fetch data from the file and put it in our buffer. 

## File Descriptor (FD)
A file descriptor is simply an integer that acts as a handle or reference to an open file.
By default:
- `0` is standard input (keyboard)
- `1` is standard output (screen)
- `2` is standard error (errors)

When we open a new file, the OS gives us the next available number (like 3). We pass this number to `read` and `write` so the OS knows which file we are talking about.

## What does `strace` do?
`strace` is a diagnostic tool on Linux. If you run `strace ./file_io`, it intercepts and logs all the system calls the program makes. It shows exactly what arguments were passed to `open`, `write`, etc., and what they returned. It's basically a way to spy on the conversation between the program and the kernel. (Since I'm on a Mac, the equivalent command is `dtruss` or `fs_usage`, but the concept is exactly the same).

## What is an Inode in the Kernel?
An inode (Index Node) is a data structure in the file system. A file name is just something for humans to read; the OS actually tracks files using inodes. 
The inode stores metadata about the file, such as:
- File size
- Permissions (who can read/write)
- Owner ID
- Creation/Modification timestamps
- **Pointers to the data blocks** on the actual disk

When we call `open("testfile.txt")`, the OS looks inside the directory to find the inode number for `testfile.txt`. Then it uses that inode to find where the actual file data lives on the disk.
