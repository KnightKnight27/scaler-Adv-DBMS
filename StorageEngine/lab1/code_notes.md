# Lab 1 — `file_io.cpp` Code Notes

Line-by-line annotations for `file_io.cpp`.

---

## Headers

| Header | Why it's here |
|--------|---------------|
| `<fcntl.h>` | `open()` and the `O_*` flags (`O_RDONLY`, `O_WRONLY`, `O_CREAT`, `O_TRUNC`) |
| `<sys/fcntl.h>` | Redundant on most systems — `fcntl.h` already covers it |
| `<unistd.h>` | `read()`, `write()`, `close()`, `fsync()` |
| `<cstring>` | `std::strlen()` |
| `<cstdio>` | `perror()` — prints the OS error message for the last failed syscall |
| `<iostream>` | `std::cout` |

---

## Write Phase

### `open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644)`
- First syscall. Kernel resolves filename → inode, creates an entry in the open-file table, and returns an **fd** — a small int that indexes into this process's fd table.
- `O_WRONLY` — write only
- `O_CREAT` — create the file if it doesn't exist
- `O_TRUNC` — wipe to length 0 if it already exists
- `0644` — permission bits (`rw-r--r--`), only applied when `O_CREAT` actually creates the file
- Return value `< 0` means the syscall failed

### `write(fd, msg, std::strlen(msg))`
- Copies bytes from user buffer → kernel **page cache**. Disk is NOT touched yet.
- The page is marked **dirty** and will be flushed lazily.
- `ssize_t` = signed `size_t` — can hold `-1` on error.

### `fsync(fd)`
- Forces all dirty pages for this fd onto the physical disk **right now**.
- Kernel hands the page to the device driver, which sets up a **DMA** transfer.
- This is the durability primitive — databases `fsync` the WAL on every commit.

### `close(fd)`
- Releases this process's fd slot.
- Does **not** fsync — that's why we called `fsync` explicitly before this.

---

## Read Phase

### `open(path, O_RDONLY)`
- Same path → same inode. `O_RDONLY` = read only.
- No mode argument since we're not creating the file.
- `fd` will likely be `3` again — the previous slot was freed by `close()`.

### `char buf[128] = {0}`
- Stack buffer, zero-initialized.
- Reading `sizeof(buf) - 1` bytes leaves room for a trailing `'\0'` so `std::cout` can treat `buf` as a C-string safely.

### `read(fd, buf, sizeof(buf) - 1)`
- Walks the chain: fd → inode → "which disk block?" → page cache lookup.
  - **Cache hit** → copy from cache to `buf` (RAM-speed, microseconds).
  - **Cache miss** → driver issues DMA into a free RAM page; if no free page, evict LRU first.
- Returns bytes actually read; `0` = EOF.
