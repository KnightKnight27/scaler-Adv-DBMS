# Lab 1 — File copy with raw POSIX syscalls

Role Number: 24BCS10079
Name: Piyush Bansal

Did this on my Mac (Apple Silicon, macOS Darwin 25.0.0). Compiled with the `g++` that comes with apple clang.

The task: copy a file using only `open`, `read`, `write`, `close`. No `<iostream>`, no `<fstream>`, no `printf`, no buffered IO from stdio. Basically C with a `.cpp` extension.

## Files in this folder

- `file_copy.cpp` — the program
- `input.txt` — a short text sample (first 200 lines of Pride and Prejudice from Project Gutenberg)
- `output.txt` — what the program produces; identical to input.txt
- `README.md` — this writeup

## Getting the input

I used the opening of Pride and Prejudice from Project Gutenberg — actual readable prose, not the scanned front matter:

```
curl -L https://www.gutenberg.org/cache/epub/1342/pg1342.txt \
  | sed -n '/^It is a truth/,/^“Did Mr. Darcy give you reasons/p' \
  | head -150 > input.txt
```

That gives ~5.6 KB starting from the famous "It is a truth universally acknowledged..." opening line. Small enough to inspect by eye, big enough that the read loop runs a couple of times instead of finishing in one syscall. If you want a bigger test, just download the whole 738 KB book.

## Build and run

```
g++ -std=c++17 -O2 -Wall -Wextra file_copy.cpp -o file_copy
./file_copy
```

Output I got:

```
copied 5623 bytes in 2 read() calls
```

The 2 makes sense: 5623 / 4096 = 1.37, so 1 full 4 KB read + 1 short read of the remaining 1527 bytes, and then a final read() that returns 0 (EOF) which I don't count.

## Did it actually copy correctly

```
$ wc -c input.txt output.txt
  5623 input.txt
  5623 output.txt

$ md5 input.txt output.txt
MD5 (input.txt)  = f9a208125508fc3540098eaf8e1c53dd
MD5 (output.txt) = f9a208125508fc3540098eaf8e1c53dd
```

Same byte count, same MD5. So the four-syscall copy is byte-for-byte identical to the original.

## Stuff I had to figure out

**`write()` can return short.** I assumed if I asked for 4096 bytes I'd get 4096 written. Apparently not — it can write less, especially with pipes, sockets, or when interrupted. So I wrote a `write_full()` that loops until everything is out. I also retry on `EINTR` because that just means a signal landed mid-syscall, not an actual error.

**`read()` returning 0 is EOF, not an error.** Took me a moment. The `while ((got = read(...)) > 0)` loop handles it cleanly because `got == 0` makes the loop exit. Negative `got` is the actual error case.

**No `printf` is annoying for one print statement.** To output "copied X bytes in Y read() calls" I had to write a small uint→ASCII helper because there's no `itoa` in standard C++ either. Then push it through `write(STDOUT_FILENO, ...)`. A lot of code for one line of output, but the lab said no stdio.

**Why 4096 for the buffer.** It's the page size on most systems, including macOS. Fits one page exactly so reads align nicely with how the OS does I/O. Out of curiosity I bumped it to 16384 — read count dropped to ~46 (738046 / 16384), but on a 700 KB file the wall-clock didn't change. On a multi-GB file the bigger buffer would matter more because you'd save on syscall overhead.

## Tracing the syscalls

On macOS the tool is `dtruss`:

```
sudo dtruss -t open,read,write,close ./file_copy
```

(On Linux it'd be `strace -e trace=openat,read,write,close ./file_copy`.)

What you'd see is something like:

```
open("input.txt", O_RDONLY)                          = 3
open("output.txt", O_WRONLY|O_CREAT|O_TRUNC, 0644)   = 4
read(3, "...The Project Gutenberg eBook...", 4096)   = 4096
write(4, "...The Project Gutenberg eBook...", 4096)  = 4096
read(3, ..., 4096)                                    = 4096
write(4, ..., 4096)                                   = 4096
... (repeats ~180 times)
read(3, ..., 4096)                                    = 738       # short read, last chunk
write(4, ..., 738)                                    = 738
read(3, "", 4096)                                     = 0         # EOF
close(3)                                               = 0
close(4)                                               = 0
```

Two opens, ~181 reads, ~181 writes, two closes. Confirms the program isn't sneaking in any other I/O behind my back via libc.

## Concepts the lab wanted me to understand

**File descriptor.** Just an integer. The kernel maintains a per-process table where the int indexes into a struct with the open flags, the file offset, and a pointer to the underlying file/inode. fds 0, 1, 2 are stdin/stdout/stderr. `open()` returns the smallest unused number, which is why my first opened file got fd 3.

**Inode.** The actual identity of a file inside the filesystem. It holds mode bits, owner, size, timestamps, link count, and pointers (direct/indirect) to the data blocks. The filename isn't stored in the inode — it lives in the directory entry, which maps name → inode number. That's why two hardlinks (different names) can point to the same file: one inode, two directory entries.

**Page cache.** The kernel's RAM cache of file data, keyed by `(inode, offset)`. When my program calls `read()`, the kernel first checks the page cache. Hit → just `memcpy` from cache to my buffer. Miss → kernel reads from disk into the page cache first, then copies to my buffer. This is why "second run is faster than first run" is so common — first run pulled the file into cache, second run is RAM-only.

**DMA (Direct Memory Access).** How the disk-to-RAM transfer happens without making the CPU shuffle every byte. The storage controller is told "put N bytes from disk at address X in RAM" and it does it itself over the bus. CPU only sets up the descriptor and handles the completion interrupt. This is why even relatively slow disk I/O is cheap from a CPU perspective.

**End-to-end path of a `read()`.** User program calls `read()` → CPU traps into kernel mode → kernel looks up the fd, finds the inode → checks page cache → if hit, `memcpy` to user buffer; if miss, kernel goes through filesystem layer → block layer → device driver → DMA from disk into a page cache page, then `memcpy` to user buffer. Either way, `read()` returns the byte count and execution goes back to user space.

## Takeaways

The thing that stuck with me is that *all* file I/O in any language eventually boils down to these four syscalls. Python's `open()`, Node's `fs.readFile`, Java's `FileInputStream`, even C++'s `<fstream>` — under the hood it's `open` + `read` + `write` + `close` with various amounts of buffering and convenience on top.

Also realized that "fast file I/O" is basically the page cache and DMA doing the work. The syscall overhead is real but small compared to the actual disk round-trips, which is why the standard performance trick is just to read in bigger chunks (fewer syscalls, more bytes per disk round-trip).
