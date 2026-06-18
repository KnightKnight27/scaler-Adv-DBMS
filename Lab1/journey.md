# Journey of Raw System Call Operations

This document explains the lifecycle of the file operations executed in `raw_io.cpp`. The objective was to avoid standard I/O libraries (like `<iostream>`, `<fstream>`, or `<stdio.h>`) entirely and perform direct communication with the operating system kernel via raw system calls.

## Overview of System Calls Used

1. `open()`: Requests the kernel to provide access to a file and returns a File Descriptor (FD).
2. `write()`: Requests the kernel to transfer bytes from a user-space buffer into an open file descriptor.
3. `read()`: Requests the kernel to transfer bytes from an open file descriptor into a user-space buffer.
4. `close()`: Informs the kernel that the process is done using the file descriptor, freeing resources.

---

## The Journey

### Phase 1: Writing Data

1. **Opening for Writing (`open`)**
   ```cpp
   int fd_write = open("testfile.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
   ```
   - **Journey:** We transition from user space to kernel space. We ask the OS to give us write access to `testfile.txt`.
   - **Flags:**
     - `O_WRONLY`: Write-only mode.
     - `O_CREAT`: Create the file if it doesn't exist.
     - `O_TRUNC`: If it exists, clear its contents to 0 bytes.
   - **Permissions (`0644`):** Read/Write for owner, Read for group/others.
   - **Result:** The OS kernel creates an entry in its file descriptor table and returns an integer (e.g., `3`) representing this open stream.

2. **Writing Data (`write`)**
   ```cpp
   ssize_t bytes_written = write(fd_write, content, len);
   ```
   - **Journey:** We pass the file descriptor (`fd_write`) and our text buffer to the kernel. The kernel then orchestrates writing these bytes to the underlying storage (or disk cache).
   - **Result:** Returns the number of bytes successfully written.

3. **Closing the File (`close`)**
   ```cpp
   close(fd_write);
   ```
   - **Journey:** We tell the OS we are done writing. The kernel flushes any remaining buffers to disk and removes the file descriptor from the process's FD table.

### Phase 2: Reading Data

4. **Opening for Reading (`open`)**
   ```cpp
   int fd_read = open("testfile.txt", O_RDONLY);
   ```
   - **Journey:** We make another system call to the kernel, asking to read `testfile.txt`.
   - **Flags:** `O_RDONLY` (Read-only mode).
   - **Result:** The OS validates file existence/permissions and returns a new file descriptor for this read session.

5. **Reading Data (`read`)**
   ```cpp
   ssize_t bytes_read = read(fd_read, buffer, sizeof(buffer) - 1);
   ```
   - **Journey:** We pass an empty character array (`buffer`) and its size to the kernel. The kernel reads data from the file on disk into this user-space buffer.
   - **Result:** Returns the number of bytes read. We use this to correctly null-terminate the character array.

6. **Printing to Standard Output (`write`)**
   ```cpp
   write(1, buffer, len);
   ```
   - **Journey:** Since we are strictly avoiding libraries like `std::cout` or `printf`, we use the `write()` system call again, but this time we pass file descriptor `1`. By POSIX convention, FD `0` is `stdin`, FD `1` is `stdout`, and FD `2` is `stderr`. This outputs our read string to the terminal.

7. **Closing the File (`close`)**
   ```cpp
   close(fd_read);
   ```
   - **Journey:** Finally, we close the reading file descriptor. The kernel cleans up the references. The program completes without ever linking to standard I/O library overhead.
