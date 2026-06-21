# Lab 1 — File I/O and the Kernel Journey

**Name:** Akshat Kushwaha
**Roll No:** 24BCS10060

## What this lab is about

When I write `std::ifstream file("notes.txt")` in C++ and read it, it feels like
one simple step. But a lot happens below that line. This lab is about following
that path: from my C++ code, through the C++ library, down to the `read()`
system call, into the kernel, and finally to the actual file on disk. I wrote a
small reader program and traced what it asks the kernel to do.

## Files

| File | What it does |
|---|---|
| `file_reader.cpp` | Opens a text file, prints it, counts lines/words/bytes, and prints the file's inode number + size using `stat()` |
| `notes.txt` | Auto-created sample input (created on first run if missing) |

## Build & run

```bash
g++ -std=c++17 -Wall -Wextra file_reader.cpp -o file_reader
./file_reader
```

Sample output:

```
----- contents of notes.txt -----
Lab 1 reads this file using std::ifstream.
Under the hood that becomes open() and read() syscalls.
The kernel resolves the name to an inode and serves bytes.
----- end of file -----

lines = 3, words = 27, bytes (approx) = 158
inode number  = 54711993
size on disk  = 158 bytes
block count   = 8 (512B blocks)
```

## What happens when I open and read a file

### 1. The system call boundary

`std::ifstream` is a C++ convenience wrapper. When I open the file it eventually
calls `open()` (actually `openat()` on Linux), and each read becomes a `read()`
system call. A **system call** is the point where my normal user program asks the
**kernel** to do something it isn't allowed to do directly (touch the disk). The
CPU switches into kernel mode, does the work, and switches back.

I can see these calls on Linux with `strace`:

```bash
strace -e trace=openat,read,close ./file_reader
```

Roughly what shows up:

```
openat(AT_FDCWD, "notes.txt", O_RDONLY) = 3
read(3, "Lab 1 reads this file...", 4096) = 144
read(3, "", 4096)                         = 0     <- 0 means end of file
close(3)                                  = 0
```

On macOS (which is what I'm on) the equivalent tool is `dtruss` (needs `sudo`),
but the idea is the same.

### 2. File descriptors

`openat(...) = 3` returns **3**. That number is a **file descriptor** — a small
integer the kernel gives me to refer to the open file. 0, 1, 2 are already taken
(stdin, stdout, stderr), so the first file I open gets 3. Every later `read()`
passes that 3 so the kernel knows which open file I mean. `close(3)` hands it
back to the kernel.

### 3. From a name to an inode

The file's real identity in the filesystem is **not** its name — it's its
**inode**. The inode is an on-disk record holding the file's size, permissions,
owner, timestamps, and pointers to the data blocks. The *name* lives in the
directory, which is basically a table mapping names → inode numbers.

So `open("notes.txt")` makes the kernel:
1. walk the directory to find the entry `notes.txt`,
2. look up the inode it points to,
3. check I'm allowed to read it,
4. set up the file descriptor.

My program prints the inode number with `stat()` so I can actually see it:

```bash
ls -i notes.txt     # shows the same inode number
stat notes.txt      # shows inode, size, blocks
```

### 4. The layers a read passes through

```
my C++ code  (std::ifstream)
      |
  C++ / libc buffering
      |
  read() system call        <-- user space ends, kernel begins
      |
  VFS  (virtual filesystem layer, a common interface)
      |
  the real filesystem driver (APFS / ext4 / ...)
      |
  page cache  (kept in RAM)
      |
  block device driver -> actual disk   (only on a cache miss)
```

### 5. The page cache (why the second run is faster)

The kernel keeps recently read file data in RAM in the **page cache**. The first
time I read a file the kernel may have to go to the disk, which is slow. The
*second* time, the data is already sitting in the page cache, so `read()` just
copies it from RAM — no disk needed. This is why running the program twice in a
row feels instant the second time.

## Key takeaways

- `std::ifstream` is a thin wrapper; underneath it is `open()` / `read()` /
  `close()` talking to the kernel.
- A **file descriptor** is just an integer handle for an open file.
- The kernel finds a file by resolving its name to an **inode**, which is the
  file's real on-disk identity.
- `read()` returning `0` means end-of-file, not an error.
- The **page cache** makes repeated reads fast because the data stays in RAM.
- `strace` (or `dtruss` on macOS) is the easiest way to actually *see* the
  syscalls your program makes.
