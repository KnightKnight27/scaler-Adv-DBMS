# File Descriptors, Syscalls and `strace` Learning Journey

## Goal

The goal was to understand low-level file handling in C++ using:

- File descriptors
- Raw syscall-style APIs
- `open()`
- `read()`
- `write()`
- `lseek()`
- `close()`
- `strace`

---

# 1. Understanding File Descriptors

In Linux/Unix systems, files are represented internally using **file descriptors**.

A file descriptor is just an integer managed by the kernel.

Common descriptors:

| FD | Meaning |
|---|---|
| `0` | stdin |
| `1` | stdout |
| `2` | stderr |

The next opened file usually gets descriptor `3`.

---

# 2. Opening a File

Used syscall-style API:

```cpp
int fd = open("file.txt", O_RDWR);
```

### Understanding

- `open()` asks the kernel to open a file.
- `O_RDWR` means:
  - read permission
  - write permission

If successful:

```cpp
fd >= 0
```

Otherwise:

```cpp
fd == -1
```

---

# 3. Reading From a File

Used:

```cpp
char buffer[100];
ssize_t bytesRead = read(fd, buffer, sizeof(buffer));
```

---

# 4. Writing To a File

Used:

```cpp
const char* newData = "New data to write to the file.";

write(fd, newData, strlen(newData));
```

### Important Realization

`write()` works on raw bytes.

Kernel:
- takes bytes from memory
- writes them into file

It does NOT:
- clear old content
- reset offsets automatically

---

# 5. Understanding File Offset

One of the biggest learnings.

Each file descriptor internally maintains a:

```text
current file offset
```

Kernel tracks:
- where next read happens
- where next write happens

---

## Offset Movement

### After `read()`

Offset moves forward by:

```text
number of bytes read
```

### After `write()`

Offset moves forward by:

```text
number of bytes written
```

---

# 6. Problem Faced

After writing:

```cpp
write(fd, newData, strlen(newData));
```

another `read()` returned nothing.

Reason:

Offset had already moved to EOF.

So:

```cpp
read(...)
```

returned:

```text
0
```

which means:

```text
EOF reached
```

---

# 7. Using `lseek()`

To manually move the offset:

```cpp
lseek(fd, 0, SEEK_SET);
```

Meaning:

- `fd` → file descriptor
- `0` → target offset
- `SEEK_SET` → relative to file beginning

This reset offset back to start of file.

After this, reading worked again.

---

# 8. Using `strace`

Used:

```bash
strace ./a.out
```

to observe actual syscalls.

This showed:

```text
openat(...)
read(...)
write(...)
lseek(...)
close(...)
```

---

# 9. Important Syscall Observations

### Opening File

```text
openat(AT_FDCWD, "file.txt", O_RDWR) = 3
```

Kernel opened file and returned descriptor `3`.

---

### Reading

```text
read(3, "...", 100) = 40
```

Meaning:
- read from fd `3`
- requested `100` bytes
- actually got `40`

---

### Writing

```text
write(3, "New data to write to the file.", 30) = 30
```

Meaning:
- wrote `30` bytes
- offset moved forward automatically

---

### Repositioning Offset

```text
lseek(3, 0, SEEK_SET) = 0
```

Offset reset to beginning.

---

### Closing File Descriptor

```text
close(3) = 0
```

Meaning:
- kernel released file descriptor `3`
- resources cleaned properly

---

# 10. Important Realization About `cout`

Even:

```cpp
cout << "Hello";
```

eventually becomes syscall:

```text
write(1, ...)
```

where:
- `1` = stdout

This demonstrated:

> High-level C++ abstractions eventually become kernel syscalls.

---

# 11. Final Program Structure

The final flow became:

```text
open
→ read
→ write
→ lseek
→ read again
→ close
```

---

# 12. Resource Cleanup

Added:

```cpp
close(fd);
```

Important because:
- releases kernel resources
- prevents descriptor leaks
- useful in long-running programs
- file descriptors are limited resources

Even though OS cleans them on process exit, explicitly closing them is good systems programming practice.