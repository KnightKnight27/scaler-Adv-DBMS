# Lab 1 — File I/O & the `read()` syscall path

---

## The big picture 

When your program says "read a file," your program **cannot actually touch the disk**. Disks, RAM hardware, network cards — only the **kernel** (the core of the OS) is allowed to talk to them. Your program runs in **user space**; the kernel runs in **kernel space**. They're separated for safety: a buggy program shouldn't be able to crash the disk.

So when you call `read()`, your program is really saying: *"Hey kernel, please go read this for me."* That request is called a **system call** (syscall). Everything in the diagram is what happens *after* you make that polite request.

---


### 1. Raw system call
A *syscall* is the only doorway from user space into kernel space. `open`, `read`, `write`, `close`, `fsync` — these are all syscalls. "Raw" just means you're calling them directly (vs. using a high-level wrapper like `std::fstream` that hides them).

### 2. File descriptor (fd)
When you call `open("demo.txt", ...)`, the kernel says "OK, I'll remember this file for you. Here's a ticket number — `3`." That ticket number is the **file descriptor**. Every later call (`read`, `write`, `close`) just passes that number, and the kernel looks up which file you meant.

- Each process has its own little table: fd `0` = stdin, `1` = stdout, `2` = stderr, then `3, 4, 5...` for files you open.
- That's why your `std::cout << "fd=" << fd` will probably print `3`.

### 3. strace
Not a part of the kernel — it's a **debugging tool**. If you run `strace ./file_io`, it prints every syscall your program makes, in order. It's like X-ray vision for "what is my program *actually* asking the kernel to do." Super useful for learning.

(On macOS the equivalent is `dtruss`, but it needs `sudo`.)

### 4. Inode (in kernel)
The filename `"demo.txt"` is just a label humans use. Internally, the kernel identifies a file by an **inode** — a small struct holding: file size, permissions, owner, timestamps, and **a list of which disk blocks hold the actual data**. Filename → inode is the first lookup `open()` does.

### 5. Block / Page (page cache)
Disks store data in fixed-size chunks called **blocks** (usually 4 KB). RAM is also organized in fixed-size chunks called **pages** (also usually 4 KB — same size on purpose).

The kernel keeps a giant in-memory cache of recently-used disk blocks called the **page cache**. When you `read()`:

- If the block is already in the page cache → kernel copies it to your buffer instantly (RAM-speed). **Cache hit.**
- If not → kernel fetches it from disk first, puts it in the cache, *then* copies to you. **Cache miss** (slow).

This is why the *second* time you read a file it's way faster.

### 6. Driver (device driver)
The kernel doesn't know how to talk to *your specific* SSD or HDD model. The **device driver** is hardware-specific code that translates "read block #5839" into the exact electrical signals your SSD understands.

### 7. DMA (Direct Memory Access)
- **Naive way:** CPU reads bytes off disk one-by-one into RAM → CPU is wasted as a copy machine.
- **Smart way:** CPU tells the disk *"put block #5839 directly into RAM at address X, ping me when done"* — the disk hardware writes to RAM **without the CPU**. That's DMA. CPU goes off and does other work while the transfer happens. This is why modern I/O is fast.

### 8. Eviction
The page cache isn't infinite — RAM is limited. When it fills up and the kernel needs to bring in a new page, it has to **evict** (kick out) an old one. The usual rule is **LRU** — Least Recently Used: whichever page hasn't been touched in the longest time gets dropped. If that page was *modified* (dirty), it gets written back to disk first.

This same idea — caching + LRU eviction — is exactly what database **buffer pools** do, just in user space. 

---

## How they all chain together for one `read()`

```
your code
   │ read(fd, buf, 128)        ← syscall (user → kernel)
   ▼
kernel looks up fd → inode
   │
   ▼
inode says "your data is in disk block #5839"
   │
   ▼
is block #5839 already in page cache?
   ├─ YES → copy from cache to buf, return ✅
   └─ NO  → ask the device driver to fetch it
              │
              ▼
         driver issues DMA request to disk
              │
              ▼
         disk writes block into a free page in RAM
              │  (if no free page → EVICT an old one first)
              ▼
         page cache now has the block → copy to buf, return ✅
```

---
