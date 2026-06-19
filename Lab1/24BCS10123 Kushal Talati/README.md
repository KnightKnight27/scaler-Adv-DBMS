# Lab 1 - File copy with raw POSIX syscalls

Role Number: 24BCS10123
Name: Kushal Talati

Did this on my Mac (Apple Silicon, Darwin 25.3.0). Compiled with the g++ that ships with apple clang.

The lab was basically: copy a file using only `open` / `read` / `write` / `close`. So no `<iostream>`, no `<fstream>`, no `printf`. Pretty much C with a .cpp extension.

## What's in this folder

- `file_copy.cpp` - the actual program
- `input.txt` - Pride and Prejudice from Project Gutenberg (https://www.gutenberg.org/files/1342/1342-0.txt). Real text, not something I made up.
- `output.txt` - whatever the program produced, should be identical to input.txt
- `file_copy` - compiled binary

## Getting the data

```
curl -L -o input.txt https://www.gutenberg.org/files/1342/1342-0.txt
```

That gives me a 721 KB text file. Big enough that the read loop runs many times instead of finishing in one read.

## Building and running

```
g++ -std=c++17 -O2 -Wall -Wextra file_copy.cpp -o file_copy
./file_copy
```

Output:

```
copied 738046 bytes in 181 read() calls
```

181 reads roughly checks out. 738046 / 4096 = 180.18 so 180 full 4 KB reads, 1 short read at the end (=181 reads with data), and then one more read that returns 0 (EOF) which isn't counted.

## Did the copy actually work

```
$ wc -c input.txt output.txt
  738046 input.txt
  738046 output.txt

$ md5 input.txt output.txt
MD5 (input.txt)  = b0d8a2cfd0fecba469f601492d8a90e1
MD5 (output.txt) = b0d8a2cfd0fecba469f601492d8a90e1
```

Same size, same hash, so yeah, it copied perfectly using just the four syscalls.

## Things that tripped me up

**write() can return less than you asked for.** I didn't know this. If you do `write(fd, buf, 4096)` it can come back with say 3000. So you have to loop until your whole buffer is out. That's why I have a `write_all` wrapper. Same loop also retries on `EINTR` (some signal interrupted the syscall) since that's not really a failure.

**read() returning 0 means EOF, not an error.** Took me a sec. The loop `while ((n = read(...)) > 0)` handles it because n becomes 0 and the loop just exits. In Java/Python you'd usually catch an exception or check for a sentinel, here it's just a normal return value.

**No printf, no cout** is harder than it sounds. To print "copied N bytes" I had to write a tiny number-to-ascii helper (`u2a`) and then send the result through `write(STDOUT_FILENO, ...)`. Kind of annoying for one line of output but that's the rule.

**4096 buffer.** I used 4 KB because that's the OS page size on most systems. Tried 16 KB out of curiosity and the read count dropped to ~46 with the same total bytes. Not noticeably faster on a 700 KB file but on a multi-GB file the difference would matter.

## Watching it actually call the syscalls

On macOS dtruss is the tool:

```
sudo dtruss -t open,read,write,close ./file_copy
```

(`strace -e trace=openat,read,write,close ./file_copy` on Linux.)

What you'd see is something like:

```
open("input.txt", O_RDONLY)                         = 3
open("output.txt", O_WRONLY|O_CREAT|O_TRUNC, 0644)  = 4
read(3, "...The Project Gutenberg eBook...", 4096)  = 4096
write(4, "...The Project Gutenberg eBook...", 4096) = 4096
read(3, ..., 4096)                                   = 4096
write(4, ..., 4096)                                  = 4096
... (this just repeats)
read(3, ..., 4096)                                   = 2046     # short read near end
write(4, ..., 2046)                                  = 2046
read(3, "", 4096)                                    = 0        # EOF
close(3)                                              = 0
close(4)                                              = 0
```

Two opens, ~181 reads, ~181 writes, two closes. Nothing else. Confirmed I'm not accidentally pulling in anything from libc that does extra I/O.

## File descriptors / inodes / page cache stuff

These are the concepts the lab wanted me to understand.

A **file descriptor** is just an int. The kernel has a per-process table where the int indexes into a struct with the open flags, the current offset, and a pointer to the underlying inode. fds 0, 1, 2 are stdin / stdout / stderr by default. open() returns the smallest unused int, which is why my first opened file was fd 3.

An **inode** is the actual file's identity inside the filesystem. It has mode bits, owner, size, timestamps, link count, and pointers to the data blocks. The filename isn't part of the inode - it lives in the parent directory entry, which maps name -> inode. That's why hardlinks work: two names, same inode.

The **page cache** is the kernel's RAM cache of file data, keyed by (inode, offset). When my program calls read(), the kernel checks the page cache first. If the page is there it's just a memcpy from cache into my buffer. If not, the kernel has to actually go to disk first. This is exactly why "second run is faster than first run" is such a common pattern - first run brought it into cache, second run is just RAM access.

**DMA** is how the disk-to-RAM transfer happens without using CPU cycles for each byte. The storage controller is told "put N bytes from disk into RAM at this address" and it just does it over the system bus. The CPU is only involved at the start (setting up the descriptor) and the end (handling the interrupt). So I/O is cheap CPU-wise even when it's slow wall-clock-wise.

So putting it together, when read() is called: the CPU traps into the kernel, the kernel finds the inode for that fd, checks the page cache, and either does a fast memcpy (cache hit) or queues a block I/O that goes through the filesystem layer, the block layer, and the device driver, which kicks off a DMA transfer from the actual hardware. Either way the bytes end up in my user buffer and read() returns the count.

Reading the Manasvi PR's notes and the man pages on my Mac (`man 2 read`) was useful here.

## Notes / takeaways

I think the main thing I got out of this is that all the I/O I do every day in higher-level languages eventually becomes these four syscalls. Python's `open()`, Node's `fs.readFile`, Java's `FileInputStream`, even C++'s `<fstream>` - underneath, it's open + read + write + close, just with more sugar on top.

The other thing is that "fast" file I/O is mostly the page cache and DMA doing the work. The syscall overhead matters but the disk round-trips matter much more, which is why batching reads into bigger chunks is the standard trick.
