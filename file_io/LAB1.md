# Lab 1 - C++ file I/O through the kernel

## Goal

Trace a tiny C++ file reader from `std::ifstream` down to the filesystem path
lookup, inode metadata, page cache, and the read syscall.

## Program

`reader.cpp` opens `test.txt` by default, or a path passed on the command line.

```bash
cd file_io
g++ -std=c++17 -Wall -Wextra -pedantic -o reader reader.cpp
./reader
```

On Linux, trace the calls made around the file:

```bash
strace -e trace=openat,read,close,fstat,mmap ./reader
```

On macOS, the closest equivalent is:

```bash
sudo dtruss -t open,read,close,fstat ./reader
```

## What the trace shows

| Kernel boundary | Why it appears |
|-----------------|----------------|
| `openat` / `open` | resolves the path, checks permissions, finds the inode, and returns a file descriptor |
| `fstat` | asks the kernel for metadata such as size, mode bits, and inode number |
| `read` | copies bytes from the file into the process buffer used by the C++ stream |
| `mmap` | maps runtime libraries or, for some workloads, file pages into the address space |
| `close` | releases the process file descriptor and drops the open-file reference |

The filename is not the file itself. It is a directory entry that points to an
inode. The inode stores the metadata and block mapping; the file descriptor is
only this process's handle to an open file object.

## Kernel path

```text
std::ifstream
  -> C/C++ runtime buffering
  -> read/open/stat syscall
  -> VFS
  -> filesystem driver
  -> page cache
  -> block device, only on cache miss
```

Repeated reads usually come from the page cache, so the same user-space code can
avoid physical disk I/O after the first warm-up read.
