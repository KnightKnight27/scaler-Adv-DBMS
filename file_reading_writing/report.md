# Low-Level File I/O in C++

The program opens a file, writes to it, reads it back, and prints the inode number — using only raw syscalls (`open`, `write`, `read`, `fstat`, `close`). No `fopen`, no `printf`.

---

## File Descriptor

When you open a file, the kernel hands you back a small integer called a file descriptor. That number is your handle for all future operations on that file. `0`, `1`, `2` are always stdin/stdout/stderr, so your first file gets `3`.

## strace

`strace` shows every syscall your program makes in real time. The relevant part of the output:

```
openat(AT_FDCWD, "test.txt", O_WRONLY|O_CREAT|O_TRUNC, 0644) = 3
write(3, "Yoooooooo!\n", 11)            = 11
close(3)                                = 0
openat(AT_FDCWD, "test.txt", O_RDONLY)  = 3
read(3, "Yoooooooo!\n", 128)            = 11
fstat(3, {st_mode=S_IFREG|0644, st_size=11, ...}) = 0
close(3)                                = 0
```

## Inode

Every file has an inode — a struct inside the kernel holding metadata like size, permissions, and where the data lives on disk. The filename is just a label; the inode number is the real identity. `fstat()` gives us this from code. `stat test.txt` in the terminal shows the same number: **Inode: 17704065**.

## Pages

The kernel never reads one byte at a time. It loads data in 4 KB chunks called pages into a RAM cache (the page cache), then hands your program just the bytes it asked for.

## Drivers

A syscall like `read()` doesn't touch the disk directly. It goes:
`your program → VFS → filesystem driver (ext4) → block driver → disk`

## DMA

When data is fetched from disk, the disk controller copies it straight into RAM without the CPU doing any of the work. The CPU just fires off the request, does other things, and gets interrupted when the data is ready.

## Eviction

RAM is finite. When the page cache fills up, the kernel removes the pages that haven't been used recently (LRU). If a page was modified, it's written to disk first. If not, it's just dropped.
