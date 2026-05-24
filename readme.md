# File_Ops — The Journey of a File

A small C++ program that uses **raw POSIX system calls** (`open`, `write`, `read`, `close`) to write to and read from a file, and documents what actually happens inside the kernel at each step.

---

## 1. What the Program Does

1. **Opens** `file.txt` for writing (`O_CREAT | O_WRONLY | O_TRUNC`, mode `0644`) — creates it if missing, truncates if it exists.
2. **Writes** the string `"I am using this file to write"` to it.
3. **Closes** the write file descriptor.
4. **Re-opens** the same file in read-only mode (`O_RDONLY`).
5. **Reads** up to 255 bytes into a buffer and prints the contents.
6. **Closes** the read file descriptor.

## 2. Build & Run

```bash
g++ main.cpp -o file_ops
./file_ops
```

After running, you'll find a `file.txt` containing the written string in the same directory.

---

## 3. The Journey of a File — What Happens Inside the Kernel

Every syscall in this program triggers a chain of work across **user space → kernel space → filesystem driver → block layer → disk**. Below is the journey for each call.

### 3.1 `open(filename, O_CREAT | O_WRONLY | O_TRUNC, 0644)`

The user-space `open()` is a thin wrapper that triggers a **trap into the kernel** (a software interrupt on x86, `svc` on ARM). Once inside the kernel:

1. **Path resolution (namei / VFS lookup):** The kernel walks the path `file.txt` starting from the current working directory's inode. Each component is looked up in the parent directory's data blocks.
2. **Inode lookup / creation:**
   - If the file exists, the kernel loads its **inode** from disk into the in-memory **inode cache**.
   - If not (and `O_CREAT` is set), the filesystem allocates a fresh inode number, initializes a new inode struct (owner UID/GID, mode `0644`, timestamps, link count = 1, size = 0), and adds a directory entry mapping `"file.txt" → inode#`.
3. **Truncation (`O_TRUNC`):** The kernel releases any data blocks the inode currently points to and sets size to 0.
4. **Open file table entry:** The kernel creates a *struct file* (the system-wide open-file description) holding the current offset, access mode, and a pointer to the inode.
5. **File descriptor table entry:** The kernel finds the lowest unused integer in the process's per-process FD table, points it at the struct file, and returns that integer.

The returned `fd` is just **an index into your process's FD table** — typically `3` (since 0/1/2 are stdin/stdout/stderr).

### 3.2 `write(fd, data, len)`

1. **FD → struct file:** Kernel uses `fd` to look up the struct file, which gives it the inode and current offset.
2. **Permission check:** Confirms the file was opened with a writable mode.
3. **Copy into page cache:** Bytes are copied from the user-space buffer (`data`) into kernel **page cache** pages associated with the inode. This is a `copy_from_user` operation.
4. **Block allocation:** If the write extends beyond currently allocated blocks, the filesystem allocates new data blocks (via the block allocator / free-block bitmap) and records their addresses in the inode's block-pointer array (direct/indirect blocks on ext-style FS, extents on ext4/xfs/APFS).
5. **Inode update:** The inode's `size`, `mtime`, and `ctime` are updated. The inode and modified pages are marked **dirty**.
6. **Offset advance:** The struct file's offset moves forward by the number of bytes written.
7. **Return:** Kernel returns the byte count written. **The data is not yet on disk** — it lives in the page cache and is flushed later by the writeback threads (or immediately if `O_SYNC` / `fsync()` is used).

### 3.3 `close(fd)` (after write)

1. **FD table cleanup:** The entry at index `fd` in the process FD table is cleared.
2. **Reference count:** The struct file's refcount is decremented. If it drops to 0 (no other FDs share it, e.g., via `dup`/`fork`), the struct file is freed.
3. **Inode reference:** The inode's in-memory refcount drops. The inode stays in the cache for fast re-open.
4. **No automatic flush:** `close()` does **not** force the page cache to disk. Use `fsync(fd)` before `close()` if you need durability.

### 3.4 `open(filename, O_RDONLY)` (second open)

Same path resolution as before, but:
- `O_CREAT` is absent, so the inode must already exist.
- A fresh struct file is allocated with offset = 0 and read-only mode.
- A new FD is returned. Since the previous FD was closed, the lowest free slot is reused — usually `3` again.

### 3.5 `read(fd, buffer, 255)`

1. **FD → struct file → inode.**
2. **Check page cache:** Kernel checks if the requested byte range is already in the page cache (very likely, since we just wrote it).
   - **Hit:** Data is copied directly from cache to the user buffer (`copy_to_user`).
   - **Miss:** Kernel issues a block-layer read (which queues a request to the disk driver), blocks the process until the block arrives, populates the page cache, then copies to user space.
3. **Offset advance:** Struct file's offset moves forward by `bytes_read`.
4. **Return:** Number of bytes actually read (may be less than requested at EOF — that's why we use the returned value, not the buffer size).

### 3.6 `close(fd)` (after read)

Same as 3.3 — FD entry cleared, struct file refcount dropped, inode stays cached.

---

## 4. Key Concept — File Descriptor

A **file descriptor (FD)** is a small non-negative integer that a process uses to refer to an open file (or socket, pipe, device, etc.). It is **not** the file itself — it is a handle/index.

Three tables are involved:

```
Process FD Table          System-wide Open File Table       Inode Cache
(per process)             (struct file)                     (per filesystem)
+-----+                   +------------------+              +---------------+
|  0  | -----> stdin ---> | offset, mode,    | -----------> | inode for     |
|  1  | -----> stdout     | flags, *f_inode  |              | "file.txt"    |
|  2  | -----> stderr     +------------------+              | size, blocks, |
|  3  | -----> our file ->                                  | mode, owner   |
+-----+                                                     +---------------+
```

- FDs `0`, `1`, `2` are pre-allocated (stdin, stdout, stderr).
- The first `open()` in our program returns `3` (lowest free index).
- After we `close(3)` and `open()` again, we get `3` back — slots are reused.
- `fork()` duplicates the FD table; `dup()`/`dup2()` create new entries pointing to the same struct file.

## 5. Key Concept — Inode in the Kernel

An **inode** (index node) is the on-disk and in-memory data structure that **describes a file** — everything *except* its name and contents:

| Field            | Description                                         |
|------------------|-----------------------------------------------------|
| `i_mode`         | File type + permission bits (e.g., `0644`, regular) |
| `i_uid` / `i_gid`| Owner user and group                                |
| `i_size`         | Length in bytes                                     |
| `i_atime/mtime/ctime` | Access / modify / change times                 |
| `i_nlink`        | Number of hard links to this inode                  |
| `i_blocks`       | Block addresses (direct, indirect, or extents)      |

Filenames live in **directory entries**, which map names → inode numbers. The same inode can be referenced by multiple names (hard links). When `nlink` hits 0 *and* no process has it open, the inode and its blocks are freed.

For our program:
- `open(O_CREAT)` allocated a fresh inode and added `"file.txt" → inode#` to the current directory.
- `write` updated `i_size`, `i_mtime`, and the block pointer list.
- `close` did **not** free the inode — it stayed in cache so the second `open` was fast.

You can see the inode number with:

```bash
ls -i file.txt
stat file.txt
```

## 6. Tracing the Syscalls — `strace` / `dtruss`

To see the actual syscalls the program issues, you can trace it.

**On Linux:**
```bash
strace -e trace=openat,write,read,close ./file_ops
```

**On macOS (this machine):** `strace` doesn't exist; the equivalent is `dtruss` (built on DTrace). It requires `sudo` and SIP may restrict it for unsigned binaries:
```bash
sudo dtruss -t open ./file_ops
sudo dtruss ./file_ops 2>&1 | grep -E 'open|read|write|close'
```

Expected trace (abbreviated):

```
open("file.txt", O_WRONLY|O_CREAT|O_TRUNC, 0644) = 3
write(3, "I am using this file to write", 29) = 29
close(3) = 0
open("file.txt", O_RDONLY) = 3
read(3, "I am using this file to write", 255) = 29
close(3) = 0
```

Things to notice in a real trace:
- The C++ standard library issues many setup syscalls (`mmap`, `mprotect`, `brk`, etc.) before `main()` runs — our four syscalls are buried in that noise.
- `open()` is often shown as `openat(AT_FDCWD, ...)` on modern Linux/macOS — same semantics, just relative to a directory FD.
- Each `std::cout` line becomes a `write(1, ...)` to stdout, FD `1`.

## 7. Files

- `main.cpp` — source code, with comments at each syscall site.
- `file.txt` — created at runtime by the program.
- `readme.md` — this document.
