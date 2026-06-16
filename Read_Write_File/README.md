# Read / Write File Demo

This program shows the basic Linux file journey using `open`, `write`, `close`, and `read`.

## What happens in `main.cpp`

- `open(..., O_CREAT | O_WRONLY | O_TRUNC)` creates or resets `test.txt`.
- The kernel returns a file descriptor and adds it to the process FD table.
- That FD points to a file description, which points to the inode on disk.
- `write(fd, ...)` copies `I am writing to this file` into the kernel and updates the file size.
- `close(fd)` releases the write descriptor.
- `open(..., O_RDONLY)` opens the same file again for reading.
- `read(fd, ...)` reads the same bytes back, usually from the page cache or disk.
- `close(fd)` releases the read descriptor.

## `strace` takeaway

- `openat()` creates `test.txt` and returns FD `3`.
- `write(3, ...)` writes 25 bytes.
- `close(3)` closes the file.
- `openat()` reopens `test.txt` in read-only mode.
- `read(3, ...)` returns the same content.

## Kernel Operations

### `open()` / `openat()`

```
User:
	fd = open("test.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644)

Kernel does:
	1. Parse pathname
	2. Find inode (create if O_CREAT)
	3. Check permissions
	4. Create file description
	5. Allocate FD in process FD table
	6. Link FD -> file description -> inode
	7. Return FD to user
```

### `write()`

```
User:
	write(fd, "I am writing to this file", 25)

Kernel does:
	1. Validate FD
	2. Get inode from FD table
	3. Check write permissions
	4. Copy data from user buffer -> kernel buffer
	5. Allocate disk blocks if needed
	6. Write to page cache (buffering)
	7. Update inode:
		 - size += 25
		 - mtime = now
	8. Return bytes written
```

### `read()`

```
User:
	read(fd, buffer, 100)

Kernel does:
	1. Validate FD
	2. Get inode
	3. Check read permissions
	4. Get current file offset
	5. Read from disk (or page cache if hit)
	6. Copy kernel buffer -> user buffer
	7. Update inode:
		 - atime = now
	8. Advance file offset
	9. Return bytes read
```

### `close()`

```
User:
	close(fd)

Kernel does:
	1. Validate FD
	2. Decrement inode reference count
	3. Remove FD from process FD table
	4. Free resources
```

## Key concepts

- **System call**: the app asks the kernel to do file I/O.
- **File descriptor**: a small number used by the process to refer to an open file.
- **Inode**: stores file metadata like size and permissions.
- **File offset**: the current read/write position inside the file.

## Run

```bash
g++ main.cpp -o main
strace -e openat,open,read,write,close ./main
```
