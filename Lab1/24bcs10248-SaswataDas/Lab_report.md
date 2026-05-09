# Lab 1: Raw System Calls for File Handling

Student: Saswata Das  
Roll No: 24bcs10248

## Aim

Write a C++ program that opens, writes, reads, and closes a file using raw system calls, and understand file descriptors, `strace`, inode, and kernel interaction.

## Submitted Files

| File | Purpose |
| --- | --- |
| `Lab_report.md` | Final report and observations |
| `file_syscall.cpp` | C++ program using raw `syscall()` file operations |
| `run.sh` | Compiles and runs the program |
| `trace.sh` | Runs the program with `strace` on Linux |
| `inode_check.sh` | Displays inode and file metadata |

## Script Usage

| Task | Command |
| --- | --- |
| Compile and run | `./run.sh` |
| Generate strace output | `./trace.sh` |
| Check inode details | `./inode_check.sh lab1_syscall_output.txt` |

Note: `run.sh` was tested on macOS. `trace.sh` requires Linux because `strace` is Linux-specific.

## Program Flow

| Step | Raw syscall used | Purpose |
| --- | --- | --- |
| Open/Create file | `SYS_openat` | Creates `lab1_syscall_output.txt` and returns a file descriptor |
| Write data | `SYS_write` | Writes text bytes into the file |
| Move offset | `SYS_lseek` | Moves file pointer back to the beginning |
| Read data | `SYS_read` | Reads file content into a buffer |
| Get metadata | `SYS_fstat` | Reads inode number and file size |
| Close file | `SYS_close` | Releases the file descriptor |

## Commands Used

```sh
g++ -std=c++17 -Wall -Wextra -pedantic file_syscall.cpp -o file_syscall
./file_syscall
strace -o strace_output.txt -e trace=openat,write,lseek,read,fstat,close ./file_syscall
ls -li lab1_syscall_output.txt
stat lab1_syscall_output.txt
```

## Expected Output

```text
File descriptor: 3
Inode number   : <inode_number>
File size      : <size> bytes

Data read from file:
Lab 1 raw system call demo
This file was opened, written, repositioned, read, and closed using syscall().
```

## Key Concepts

| Concept | Meaning |
| --- | --- |
| File Descriptor | Small integer returned by the kernel after opening a file. It is used for later `read`, `write`, and `close` calls. |
| Raw System Call | Direct request from user program to kernel using `syscall()`, instead of C++ file APIs like `fstream`. |
| Inode | Kernel filesystem object that stores metadata such as inode number, size, permissions, and timestamps. |
| `strace` | Linux tool that shows system calls made by a program, useful for verifying kernel-level operations. |
| Kernel Role | The kernel validates the request, accesses filesystem structures, updates offsets/metadata, and returns results to the process. |

## strace Observation

Expected important calls in `strace_output.txt`:

```text
openat(...)
write(...)
lseek(...)
read(...)
fstat(...)
close(...)
```

These calls confirm that the program performs file handling through kernel system calls.

## Conclusion

The program demonstrates file creation, writing, reading, metadata lookup, and closing using raw system calls. File descriptors act as handles between the process and kernel, while inodes represent the actual file metadata on disk.

