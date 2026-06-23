# RAW SYSTEM CALLS LAB
**Name:** Patel Jash | **Batch:** A | **Roll:** 24BCS10632 | **Lab:** 01 | **Title:** Storage_Engine

> In this lab I (Jash) worked with low level Linux file operations using raw system calls in C++.
> Rather than relying on high level abstractions like `ifstream` or `ofstream`, I directly invoked Linux syscalls such as:
>
> * `open()`
> * `write()`
> * `read()`
> * `lseek()`
> * `close()`
>
> Doing this gave me a clearer picture of how file operations actually happen between a program and the operating system.

---

# COMPILATION

```bash
g++ main.cpp -std=c++17 -o app
```

* `-std=c++17` specifies which C++ standard to use.
* `-o app` lets us choose a custom name for the compiled binary.

---

# 1. OPEN FILE

```cpp
int fileFd = open(
    "jash-det.txt",
    O_RDWR | O_CREAT,
    0644
);
```

Here I used the `open()` syscall directly rather than any high level file library.

---

## Things I learned

* `open()` is a low level Linux system call.
* It gives back a small integer known as:

# File Descriptor (FD)

Example:

```text
3
```

---

## What is FD?

FD is essentially a reference or handle to an opened file.

Rather than interacting with the file directly, we pass this FD into other syscalls.

Example:

```cpp
write(fileFd, ...)
read(fileFd, ...)
close(fileFd)
```

---

## Meaning of Flags

```cpp
O_RDWR
```

indicates:

* open the file for reading
* open the file for writing

---

```cpp
O_CREAT
```

indicates:

* if the file doesn't exist yet, create it

---

## File Permission

```cpp
0644
```

This is only relevant when a new file is being created.

It specifies:

* the owner gets read and write access
* everyone else gets read-only access

---

## Important Observation

If the open call fails:

```cpp
fileFd < 0
```

Linux gives back `-1`.

---

# 2. WRITE TO FILE

```cpp
write(fileFd, data, strlen(data));
```

This syscall pushes bytes into the file.

---

## Why `const char*` instead of string?

I found out that kernel-level syscalls operate directly on raw memory addresses and bytes.

`std::string` is a high level wrapper/container.

But:

```cpp
const char*
```

points straight to the memory location of the characters.

That's why the syscall can work with it properly.

---

## Another Observation

I also tried:

```cpp
data2.c_str()
```

which does the conversion:

```text
string → const char*
```

so that the syscall can accept it.

---

## Offset Concept

After the first `write()`:

```text
file offset shifted forward
```

So the second `write()` continued from the new position rather than overwriting the earlier content.

This made me realise that Linux maintains a current position pointer internally for every open file.

---

# 3. MOVING FILE OFFSET

```cpp
lseek(fileFd, 0, SEEK_SET);
```

This syscall lets us manually reposition the file's current offset.

---

## Meaning

```cpp
SEEK_SET
```

indicates:

* measure the offset from the very beginning of the file

---

So this line:

```cpp
lseek(fileFd, 0, SEEK_SET);
```

essentially resets the offset back to the start of the file.

Now any subsequent `read()` or `write()` will begin from the beginning again.

---

# 4. READ FILE

```cpp
read(fileFd, buf, sizeof(buf)-1);
```

This syscall pulls bytes from the file into memory.

---

## Buffer Concept

```cpp
char buf[1024];
```

I provided the OS a chunk of memory where incoming file data can be placed.

---

## Why `char[]`?

Because syscalls deal with:

* raw bytes
* direct memory addresses

rather than high level string objects.

---

## Null Terminator

```cpp
buf[readBytes] = '\0';
```

This was required so that:

```cpp
cout << buf
```

understands where the real content stops.

Without it, random garbage values could get printed.

---

# 5. CLOSE FILE

```cpp
close(fileFd);
```

This shuts down the opened file descriptor.

---

## Why Important?

Closing the FD frees up the resources the kernel was holding.

If we skip closing:

* those resources stay occupied
* it can lead to memory or resource leaks

---

# FILE DESCRIPTORS

One of the key takeaways I got from this lab was:

# Linux manages files through File Descriptors.

---

## Internal Simplified Flow

```text
Process
   ↓
File Descriptor
   ↓
Kernel
   ↓
inode
   ↓
Actual File Data
```

---

## Important Point

FD is NOT the actual file.

It is simply:

* an integer-based handle
* that the kernel uses to identify which file is open

---

# INODE

I also came across the concept of inode.

An inode is like a file's internal identity card inside Linux.

It holds:

* file size
* permissions
* timestamps
* disk block locations

but not the filename itself.

---

## Checking inode

Command:

```bash
ls -i jash-det.txt
```

Example:

```text
104371 jash-det.txt
```

Here:

```text
104371
```

is the inode number.

---

# STRACE

I ran:

```bash
strace ./app
```

to watch the syscalls being made under the hood.

---

## Some syscalls I noticed

```text
openat()
read()
write()
close()
mmap()
```

Even though I couldn't fully decode every single line, it clearly showed that a lot of low level OS operations take place internally even for basic programs.

---

# WHAT I UNDERSTOOD FROM THIS LAB

* High level file libraries are built on top of system calls internally.
* Linux interacts with files through File Descriptors.
* Syscalls communicate directly with the kernel.
* The OS internally tracks the current offset for each open file.
* Every file has an internal inode identity.
* Reading and writing files involves kernel level operations.
* Even basic file handling is tightly connected to Operating System internals.

---

# FINAL THOUGHT

Before doing this lab I assumed file handling was just:

```cpp
fstream file;
```

But now I realise that under the surface a lot of low level mechanisms are at play like:

* syscalls
* kernel interaction
* offsets
* inode resolution
* memory buffers
* file descriptors

This lab gave me a significantly better understanding of how Operating Systems and storage systems actually coordinate with each other.