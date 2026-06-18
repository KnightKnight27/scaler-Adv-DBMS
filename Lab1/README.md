# SYSTEM CALLS FOR FILE OPERATIONS LAB

> This laboratory exercise delves into low-level file manipulation on Linux using direct system calls in C++.
> Rather than relying on high-level abstractions such as `ifstream` or `ofstream`, we utilized Linux system calls directly, including:
>
> * `open()`
> * `write()`
> * `read()`
> * `lseek()`
> * `close()`
>
> This approach provided insight into the internal mechanics of file operations between the application and the operating system.

---

# BUILDING THE PROGRAM

```bash
g++ main.cpp -std=c++17 -o app
```

* `-std=c++17` specifies the C++ standard version.
* `-o app` assigns a custom name to the executable file.

---

# 1. FILE OPENING

```cpp
int fd = open(
    "file1.txt",
    O_RDWR | O_CREAT,
    0644
);
```

In this section, `open()` was employed instead of high-level file handling libraries.

---

## Key Learnings

* `open()` is a fundamental Linux system call.
* It provides a small integer known as the:

# File Descriptor (FD)

For instance:

```text
3
```

---

## Understanding FD

A File Descriptor serves as a representative or handle for the opened file.

Instead of interacting with the file directly, this FD is used in subsequent system calls.

Examples include:

```cpp
write(fd, ...)
read(fd, ...)
close(fd)
```

---

## Flag Explanations

```cpp
O_RDWR
```

Indicates:

* Enable reading from the file
* Enable writing to the file

---

```cpp
O_CREAT
```

Indicates:

* Create the file if it does not already exist

---

## Permission Settings

```cpp
0644
```

This is relevant only during file creation.

It specifies:

* Owner has read and write permissions
* Others have read-only permissions

---

## Error Handling

When file opening fails:

```cpp
fd < 0
```

The system returns `-1`.

---

# 2. WRITING TO FILE

```cpp
write(fd, msg, strlen(msg));
```

This system call transfers bytes to the file.

---

## Rationale for `const char*` over string

I discovered that the kernel and system calls operate with raw memory addresses and byte data.

`std::string` represents a high-level container.

However:

```cpp
const char*
```

provides the direct memory location of the characters.

This is why the system call can process it correctly.

---

## Additional Testing

I also experimented with:

```cpp
msg2.c_str()
```

which performs:

```text
string → const char*
```

conversion, allowing the system call to utilize it.

---

## Offset Behavior

Following the initial `write()`:

```text
the file offset advances
```

Consequently, the next `write()` occurs from the subsequent position rather than replacing the prior content.

This demonstrated that Linux maintains an internal current position pointer for each open file.

---

# 3. ADJUSTING FILE OFFSET

```cpp
lseek(fd, 0, SEEK_SET);
```

This system call allows manual adjustment of the current file position.

---

## Interpretation

```cpp
SEEK_SET
```

means:

* Begin counting from the file's start

---

Thus, the command:

```cpp
lseek(fd, 0, SEEK_SET);
```

resets the offset to the file's beginning.

Subsequent `read()` or `write()` operations will commence from the start.

---

# 4. READING FROM FILE

```cpp
read(fd, buffer, sizeof(buffer)-1);
```

This system call retrieves bytes from the file into memory.

---

## Buffer Usage

```cpp
char buffer[1024];
```

A memory space was allocated for the operating system to store incoming file data.

---

## Reason for `char[]`

System calls operate with:

* Raw bytes
* Memory addresses

rather than high-level string objects.

---

## Null Termination

```cpp
buffer[bytesRead] = '\0';
```

This ensures that:

```cpp
cout << buffer
```

recognizes the end of the actual content.

Without it, extraneous data might be displayed.

---

# 5. CLOSING THE FILE

```cpp
close(fd);
```

This terminates the file descriptor.

---

## Significance

Closing the FD frees up kernel resources.

Failure to close properly can result in:

* Persistent resource occupation
* Potential resource leakage

---

# FILE DESCRIPTORS OVERVIEW

A major insight from this lab is that:

# Linux manages files through File Descriptors.

---

## Simplified Internal Process

```text
Process
   ↓
File Descriptor
   ↓
Kernel
   ↓
inode
   ↓
Actual File Content
```

---

## Crucial Note

The FD is not the file itself.

It is merely:

* An integer identifier
* Used by the kernel to track the open file

---

# INODE CONCEPT

I also gained knowledge about inodes.

An inode serves as the internal identifier for a file within Linux.

It contains:

* File size details
* Access permissions
* Time stamps
* Disk block information

excluding the filename.

---

## Inode Verification

Use this command:

```bash
ls -i file1.txt
```

Sample output:

```text
104371 file1.txt
```

Here:

```text
104371
```

represents the inode number.

---

# STRACE ANALYSIS

I employed:

```bash
strace ./app
```

to monitor internal system calls.

---

## Observed System Calls

```text
openat()
read()
write()
close()
mmap()
```

Although not every detail was fully comprehensible, it revealed that numerous low-level OS operations occur internally, even in basic programs.

---

# LESSONS FROM THIS EXERCISE

* High-level file libraries ultimately rely on system calls.
* Linux uses File Descriptors for file communication.
* System calls interface directly with the kernel.
* The OS tracks the current file offset internally.
* Files possess inode-based internal identities.
* File I/O involves kernel-level processes.
* Basic file operations are intricately linked with Operating System functionality.

---

# CONCLUDING REFLECTIONS

Prior to this lab, file handling seemed straightforward:

```cpp
fstream file;
```

However, I now recognize the underlying complexity involving:

* System calls
* Kernel interactions
* Offset management
* Inode resolution
* Memory buffers
* File descriptors

This exercise provided a profound understanding of the collaboration between Operating Systems and storage mechanisms.
