# Linux File I/O Demonstration

This example illustrates the lifecycle of a file in Linux using the fundamental system calls: `open()`, `write()`, `read()`, and `close()`.

## Program Workflow

### Creating and Writing to a File

The program first opens `test.txt` using:

```cpp
open("test.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);
```

This operation creates the file if it does not already exist. If it does exist, its previous contents are cleared because of the `O_TRUNC` flag.

The kernel assigns a file descriptor (FD) and records it in the process's file descriptor table. Internally, that descriptor references a file object, which ultimately points to the file's inode.

The program then writes the text:

```text
I am writing to this file
```

The data is copied into kernel-managed buffers, and the file's metadata is updated to reflect the new content and size.

After writing, the file descriptor is closed, releasing the associated resources.

---

### Reading the File

The same file is opened again, this time in read-only mode:

```cpp
open("test.txt", O_RDONLY);
```

The kernel returns another file descriptor that references the file.

A `read()` call retrieves the stored bytes and places them into a user-space buffer. Depending on system state, the data may come directly from memory (page cache) or be fetched from disk.

Finally, the descriptor is closed once more.

---

## Observing the Program with strace

Running the program under `strace` reveals the system calls being made:

1. `openat()` creates or opens `test.txt` and returns file descriptor `3`.
2. `write(3, ...)` stores the data in the file.
3. `close(3)` releases the descriptor.
4. `openat()` opens the file again for reading.
5. `read(3, ...)` retrieves the previously written content.
6. `close(3)` cleans up the descriptor.

---

# Internal Kernel Actions

## open()

Application call:

```cpp
fd = open("test.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);
```

Kernel responsibilities:

1. Resolve the file path.
2. Locate the file inode or create a new one when requested.
3. Verify access permissions.
4. Create a file object describing the open file.
5. Reserve an available file descriptor.
6. Connect the descriptor to the file object and inode.
7. Return the descriptor number to the application.

---

## write()

Application call:

```cpp
write(fd, "I am writing to this file", 25);
```

Kernel responsibilities:

1. Confirm the descriptor is valid.
2. Locate the corresponding file object.
3. Ensure write access is permitted.
4. Copy data from user memory into kernel memory.
5. Allocate storage blocks if required.
6. Store data in the page cache.
7. Update metadata such as:

   * file size
   * modification timestamp (`mtime`)
8. Return the number of bytes successfully written.

---

## read()

Application call:

```cpp
read(fd, buffer, 100);
```

Kernel responsibilities:

1. Validate the file descriptor.
2. Locate the associated inode.
3. Check read permissions.
4. Determine the current file position.
5. Retrieve data from cache or disk.
6. Copy data into the application's buffer.
7. Update access time (`atime`).
8. Advance the file offset.
9. Return the number of bytes read.

---

## close()

Application call:

```cpp
close(fd);
```

Kernel responsibilities:

1. Verify the descriptor.
2. Remove it from the process's descriptor table.
3. Decrease references to the file and inode.
4. Release resources when no longer needed.

---

# Important Concepts

### System Call

A controlled interface through which a user program requests services from the operating system kernel.

### File Descriptor

A small integer identifier used by a process to interact with an open file.

### Inode

A filesystem structure that stores metadata such as ownership, permissions, timestamps, and file size.

### File Offset

The current position within a file from which the next read or write operation will occur.

---

# Compilation and Execution

Compile:

```bash
g++ main.cpp -o main
```

Trace file-related system calls:

```bash
strace -e openat,read,write,close ./main
```

This trace provides a clear view of how user-space file operations translate into kernel-level activity.
