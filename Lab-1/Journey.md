# Journey Through a Low-Level File Copy Program

This lab explores file handling without `iostream` or `fstream`. Instead, the program in `syscall_file_demo.cpp` uses low-level system-call style APIs:

- `open`
- `read`
- `write`
- `close`
- `fstat`

The goal is to show what happens when user-space code talks to the kernel directly through file descriptors.

## What the Program Does

The program accepts two paths:

```bash
./syscall_file_demo <input-file> <output-file>
```

It performs these steps:

1. Opens the input file in read-only mode.
2. Uses `fstat` to inspect metadata such as inode and size.
3. Opens the output file with create + truncate flags.
4. Reads data from the input file in chunks.
5. Writes each chunk to the output file.
6. Closes both file descriptors.

## Why Low-Level Calls Matter

C++ file streams are convenient, but they hide the operating system details. This program keeps those details visible:

- You see how a file becomes a small integer handle called a file descriptor.
- You see how data moves between user space and kernel space.
- You see how metadata like inode and file size can be queried separately from file contents.

## File Descriptor

A file descriptor is a non-negative integer returned by the kernel.

Examples:

- `0` = standard input
- `1` = standard output
- `2` = standard error

When `open` succeeds, it returns a new descriptor such as `3`, `4`, or `5`.
That number is just a handle; the real file object lives inside the kernel.

In the program:

```cpp
const int input_fd = ::open(input_path, O_RDONLY);
```

`input_fd` is used later by `read` and `fstat`.

## Kernel Journey

When the program calls `open`:

1. The CPU switches from user mode to kernel mode.
2. The kernel checks permissions and resolves the path.
3. The kernel finds the file’s inode.
4. The kernel allocates a file descriptor for the process.
5. Control returns to user space with the descriptor value.

When the program calls `read`:

1. The kernel locates the open file object using the descriptor.
2. It reads bytes from the file into the user buffer.
3. The number of bytes read is returned to the program.

When the program calls `write`:

1. The kernel copies bytes from the user buffer into the target file.
2. The file offset advances.
3. The number of bytes written is returned.

When the program calls `close`:

1. The kernel releases the descriptor for that process.
2. Buffered state may be flushed if needed.
3. The file object may be destroyed if no one else is using it.

## Inode

An inode is a filesystem structure that stores metadata for a file.

It usually contains:

- file type
- permissions
- owner and group
- size
- timestamps
- block locations or references

The inode does **not** store the filename itself. The filename is stored in a directory entry that points to the inode.

In the program, `fstat` exposes inode information:

```cpp
struct stat input_stat {};
if (::fstat(input_fd, &input_stat) == 0) {
    print_file_info(STDOUT_FILENO, "Input", input_stat);
}
```

The output includes the inode number and file size so you can connect the visible file path to the underlying filesystem object.

## `strace` and System Call Tracing

`strace` is a Linux tracing tool that shows every system call a program makes.

Example on Linux:

```bash
strace ./syscall_file_demo input.txt output.txt
```

You would see calls such as:

- `openat`
- `read`
- `write`
- `close`
- `fstat`

That trace is useful because it makes the kernel boundary visible.

### macOS Note

This workspace is on macOS, where `strace` is not the default tool. The closest built-in alternative is usually `dtruss`:

```bash
sudo dtruss ./syscall_file_demo input.txt output.txt
```

The idea is the same: observe the system calls made by the process.

## Code Walkthrough

### 1. Manual string helpers

The program avoids `std::string` and `iostream`, so it includes tiny helper functions like:

- `c_string_length` to count characters
- `write_all` to retry partial writes
- `write_unsigned` to print numbers without `printf`

These helpers exist because low-level I/O works with raw buffers, not higher-level objects.

### 2. Opening the input file

```cpp
const int input_fd = ::open(input_path, O_RDONLY);
```

This asks the kernel for read access.
If it fails, the program exits early.

### 3. Reading metadata with `fstat`

```cpp
::fstat(input_fd, &input_stat)
```

This collects file metadata from the open descriptor. The program prints the inode and size so the filesystem concept stays visible.

### 4. Opening the output file

```cpp
const int output_fd = ::open(output_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
```

Flags used here:

- `O_WRONLY` = write only
- `O_CREAT` = create the file if it does not exist
- `O_TRUNC` = clear old contents before writing

The mode `0644` gives read/write access to the owner and read access to others.

### 5. Copy loop

The main loop repeatedly:

- calls `read` into a buffer
- checks how many bytes were returned
- calls `write_all` to send those bytes to the output file

This is the core file-transfer logic.

### 6. Closing files

```cpp
::close(input_fd);
::close(output_fd);
```

Closing the descriptors tells the kernel the process is finished with those files.

## How to Build and Run

### Build

```bash
cd /Users/ayaansingh_03/Desktop/A-DBMS/Lab-1
c++ -std=c++17 -Wall -Wextra -pedantic syscall_file_demo.cpp -o syscall_file_demo
```

### Run

```bash
./syscall_file_demo input.txt output.txt
```

If `input.txt` contains text, the program copies it to `output.txt` and prints metadata about the input file.

## Key Takeaways

- A file descriptor is just a kernel-managed handle.
- `open`, `read`, `write`, and `close` are the core low-level file operations.
- Inodes hold file metadata, not filenames.
- `strace`/`dtruss` help you observe the system-call boundary.
- Manual buffer handling teaches what higher-level file APIs hide.
