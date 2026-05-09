# Lab 1: Raw System Call File I/O

## Overview

Demonstrates file I/O using raw POSIX system calls — `open`, `read`, `write`, and `close` — without any C standard library buffering. The program reads up to 512 bytes from `file.txt`, prints them, then appends `"abcd\n"` to the same file.

## Environment

- OS: macOS / Linux
- Compiler: g++ / clang++
- Tools: strace (Linux), dtruss (macOS)

---

## What's Inside

| File | Purpose |
|------|---------|
| `raw_syscall_io.cpp` | Demo program using raw syscalls for reading and appending to a file |

---

## How the Code Works

### Constants

```cpp
static const char* TARGET_FILE = "file.txt";
static const int READ_LIMIT = 512;
```

### Helper: `open_file()`

A small wrapper around `open(2)` that prints to `stderr` on failure and returns the fd (negative on error).

```cpp
static int open_file(const char* path, int flags) {
    int descriptor = open(path, flags);
    if (descriptor < 0)
        fprintf(stderr, "open() failed with errno: %d\n", errno);
    return descriptor;
}
```

### Read Phase

```cpp
int fd = open_file(TARGET_FILE, O_RDONLY);
ssize_t n = read(fd, buf, READ_LIMIT);  // reads up to 512 bytes
buf[n] = '\0';
printf("%zd bytes read\n", n);
fputs(buf, stdout);
close(fd);
```

- Opens `file.txt` read-only
- Reads up to 512 bytes into `buf`
- Null-terminates and prints the content

### Write Phase

```cpp
fd = open_file(TARGET_FILE, O_WRONLY | O_APPEND);
const char append_data[] = "abcd\n";
ssize_t written = write(fd, append_data, sizeof(append_data) - 1);
printf("%zd bytes written\n", written);
close(fd);
```

- Reopens `file.txt` in append mode
- Writes exactly 5 bytes (`"abcd\n"`) — the `- 1` excludes the null terminator

---

## Concepts Covered

### File Descriptors
An fd is an integer index into the process's open-file table.

```
fd 0 = stdin
fd 1 = stdout
fd 2 = stderr
fd 3 = first file your code opens
```

### `O_APPEND` vs `O_WRONLY`
`O_WRONLY` alone would overwrite from position 0. `O_WRONLY | O_APPEND` atomically seeks to EOF before every `write`, so existing content is preserved.

### `ssize_t` vs `int`
`read(2)` and `write(2)` return `ssize_t` (signed size), not `int`. Using `ssize_t` avoids sign/size mismatch warnings and correctly represents `-1` on error.

### Error Handling
Every syscall return value is checked. On error, `errno` holds the reason code and is printed to `stderr`. The fd is always `close()`d before returning on error to avoid leaking it.

---

## Build and Run

```bash
# Requires file.txt to exist before running
echo "hello world" > file.txt

# Compile
g++ -o raw_io raw_syscall_io.cpp

# Run
./raw_io

# Run under strace (Linux) — see every syscall
strace ./raw_io

# Only file-related syscalls
strace -e trace=openat,read,write,close ./raw_io

# Syscall counts and time
strace -c ./raw_io
```

### Expected strace output

```
openat(AT_FDCWD, "file.txt", O_RDONLY)        = 3
read(3, "hello world\n", 512)                  = 12
close(3)                                       = 0
openat(AT_FDCWD, "file.txt", O_WRONLY|O_APPEND) = 3
write(3, "abcd\n", 5)                          = 5
close(3)                                       = 0
```

---

## Experiments

1. **Verify append:** `cat file.txt` after running — `"abcd\n"` should appear at the end each time.
2. **Run twice:** run `./raw_io` again and `cat file.txt` — a second `"abcd\n"` is appended, confirming `O_APPEND` never truncates.
3. **Count syscalls:** `strace -c ./raw_io` — notice only 2 `openat`, 1 `read`, 1 `write`, 2 `close`.
4. **Watch the inode:** `ls -i file.txt` before and after — same inode number because no new file is created.
5. **Missing file:** delete `file.txt` and run — observe the `open() failed with errno: 2` (`ENOENT`) error path.
