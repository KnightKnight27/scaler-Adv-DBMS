# Raw File I/O Syscalls

Three small programs that use POSIX file I/O syscalls (`open`, `read`, `write`, `close`) directly — no `FILE*`, no `fstream`, and no buffering from the C/C++ standard library.

## Why bypass the standard library?

Convenience APIs like `fopen` and `ifstream` hide what the kernel actually works with: integer file descriptors and raw byte buffers. These demos operate at that layer so you can see fds, partial reads and writes, `errno`, and `EINTR` in action.

**Headers:** `<fcntl.h>` for `open()` and its flags; `<unistd.h>` for `read`, `write`, `close`, and `sleep`.

## Programs

### `open.cpp` — what is a file descriptor?

Opens a path with `O_RDONLY`, prints the returned fd and process pid, then sleeps for 30 seconds.

```bash
g++ -Wall -o open open.cpp
./open somefile.txt

# In another terminal, while the process is sleeping:
lsof -p <pid>
```

While the process is alive, you can inspect its open fd in the kernel's file-descriptor table.

### `read.cpp` — a minimal `cat`

Opens a file and copies it to stdout in a 4 KB loop using `read()` and `write()`. Handles:

- `read() == 0` → end of file
- `EINTR` → retry the syscall
- partial `write()` → loop until every byte is flushed

```bash
g++ -Wall -o read read.cpp
./read somefile.txt
```

### `write.cpp` — create and write a file

Opens with `O_WRONLY | O_CREAT | O_TRUNC` (mode `0644`) and writes the given text, looping to handle partial writes.

```bash
g++ -Wall -o write write.cpp
./write out.txt "hello world"
```

## Build all

```bash
g++ -Wall -o open  open.cpp
g++ -Wall -o read  read.cpp
g++ -Wall -o write write.cpp
```

## Key points

- `open()` returns an integer fd, or `-1` with `errno` set.
- `read()` and `write()` may transfer fewer bytes than requested — always loop.
- Always `close()` the fd; on failure, check `errno` (e.g. `strerror(errno)`).
- `O_CREAT` requires a mode argument; `O_TRUNC` clears an existing file before writing.
