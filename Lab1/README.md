# Lab 1: C++ File I/O Traced with strace

## Aim
To perform file I/O using Linux system calls in C and trace the full syscall journey — from inode resolution through the VFS layer and page cache — using `strace`.

---

## The Journey: What Happens When You Call open()

```
Your Program
    │
    ▼
open("file.txt", O_RDONLY)        ← glibc wrapper
    │
    ▼
openat() syscall                   ← enters kernel space
    │
    ▼
VFS (Virtual File System)          ← kernel's unified file interface
    │  - looks up filename in dcache (directory entry cache)
    │  - resolves the inode (unique file ID on disk)
    ▼
Page Cache                         ← kernel's RAM cache for file data
    │  - HIT  → return data from RAM directly
    │  - MISS → read from disk → store in page cache → return
    ▼
File Descriptor returned to process
```

---

## System Calls Used

| System Call | Kernel Action |
|-------------|--------------|
| `openat()`  | VFS lookup → inode resolution → fd assigned |
| `read()`    | page cache check → copy to user buffer |
| `write()`   | write to page cache → mark page dirty |
| `close()`   | release fd → decrement inode ref count |

---

## Files

| File | Purpose |
|------|---------|
| `file_handling.c` | Core C program using open/read/write/close |
| `run_strace.sh`   | Compiles, runs under strace, explains the journey |

---

## How to Run

```bash
# install strace if needed
sudo apt install strace

chmod +x run_strace.sh
./run_strace.sh
```

---

## Sample strace Output

```
openat(AT_FDCWD, "file.txt", O_RDONLY) = 3  <0.000042>
read(3, "Hello from file.txt!...", 511)      = 51 <0.000018>
close(3)                                     = 0  <0.000009>
openat(AT_FDCWD, "file.txt", O_WRONLY|O_APPEND) = 3 <0.000031>
write(3, "\nThis line was appended...", 50)  = 50 <0.000014>
close(3)                                     = 0  <0.000008>
```

The `-T` flag shows time spent in each syscall. Notice `read()` is fast because the file was already in the **page cache** from the first `openat()`.

---

## Key Observations

- `openat` is what the kernel actually sees — glibc wraps `open()` into `openat()` internally.
- The file descriptor returned (3) is because 0=stdin, 1=stdout, 2=stderr are already taken.
- `write()` returns immediately — data goes to page cache first, not disk. This is called **write-back caching**.
- `close()` does NOT guarantee data is on disk. Use `fsync(fd)` if you need that guarantee.
- Running the program twice makes the second `read()` even faster — page cache still has the data from the first run.

---

## inode

Every file on Linux has an inode — a data structure storing:
- File size, permissions, timestamps
- Pointers to the actual data blocks on disk
- Reference count (how many names point to this file)

```bash
ls -li file.txt   # shows inode number
stat file.txt     # shows full inode details
```

---

## System Calls vs Standard Library

| | System Calls | Standard Library |
|-|-------------|-----------------|
| Function | `open`, `read`, `write` | `fopen`, `fread`, `fwrite` |
| Buffering | None (unbuffered) | Buffered in userspace |
| Kernel entry | Every call | Only when buffer fills |
| Visibility in strace | Yes | Wrapped, less visible |

---

## Author
Submitted as part of Lab 1 – Advanced DBMS Lab  
Date: May 2026