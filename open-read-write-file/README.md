# Raw File I/O Syscalls

A small assignment exploring the POSIX file-I/O syscalls (`open`, `read`, `write`, `close`) directly — no `FILE*`, no `fstream`, no buffering from the C/C++ standard library.

## Why raw syscalls?

`fopen`/`ifstream` are convenient wrappers, but the kernel only knows about file descriptors (integers) and byte buffers. These demos drop down to that layer so the behaviour (fds, partial reads/writes, `errno`, `EINTR`) is visible.

Headers used: `<fcntl.h>` for `open()` and its flags, `<unistd.h>` for `read`/`write`/`close`/`sleep`.

## The three demos

### `open_demo.cpp` — what is a file descriptor?
Opens a file `O_RDONLY`, prints the returned fd and the process pid, then sleeps 30s.

```
./open_demo somefile.txt
# in another terminal:
lsof -p <pid>
```
Lets you see the open fd in the kernel's table while the process is alive.

### `read_demo.cpp` — a minimal `cat`
Opens a file and copies it to stdout in a 4 KB loop using `read()` + `write()`. Handles:
- `read() == 0` → EOF
- `EINTR` → retry
- partial `write()` → loop until all bytes flushed

```
./read_demo somefile.txt
```

### `write_demo.cpp` — creating and writing a file
Opens with `O_WRONLY | O_CREAT | O_TRUNC` (mode `0644`) and writes the given text, again looping to handle partial writes.

```
./write_demo out.txt "hello world"
```

## Build

```
g++ -Wall -o open_demo  open_demo.cpp
g++ -Wall -o read_demo  read_demo.cpp
g++ -Wall -o write_demo write_demo.cpp
```

## Takeaways

- `open()` returns an `int` fd, or `-1` with `errno` set.
- `read()`/`write()` can return fewer bytes than requested — always loop.
- Always `close()` the fd; check `errno` (`strerror(errno)`) on failure.
- `O_CREAT` needs a mode argument; `O_TRUNC` empties an existing file.
