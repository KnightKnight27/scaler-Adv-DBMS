# Advanced DBMS Lab 1

## Topic
File I/O, System Calls, and the Storage Journey

## Objective
The goal of this lab is to understand how a simple file operation in a program travels through the Java runtime, the operating system, the file system, and finally storage. The point is not just to write a file, but to understand what really happens underneath.

## Implementation

### Java Program
The program below uses `FileOutputStream` to write data to a file and `FileInputStream` to read it back. It avoids higher-level input helpers so the file path is visible in the simplest possible way.

```java
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;

public class FileIOAssignment {
    private static final String FILE_NAME = "assignment-data.txt";

    public static void main(String[] args) {
        String content = "Advanced DBMS Lab 1\n"
                + "This file was written using FileOutputStream and read using FileInputStream.\n"
                + "It demonstrates how user code reaches the operating system through system calls.\n";

        try {
            writeFile(FILE_NAME, content);
            String readBack = readFile(FILE_NAME);

            System.out.println("File written successfully: " + new File(FILE_NAME).getAbsolutePath());
            System.out.println("\nRead back from file:\n");
            System.out.println(readBack);
        } catch (IOException e) {
            System.err.println("File I/O failed: " + e.getMessage());
            e.printStackTrace();
        }
    }

    private static void writeFile(String fileName, String data) throws IOException {
        try (FileOutputStream outputStream = new FileOutputStream(fileName)) {
            outputStream.write(data.getBytes(StandardCharsets.UTF_8));
            outputStream.flush();
        }
    }

    private static String readFile(String fileName) throws IOException {
        File file = new File(fileName);
        byte[] buffer = new byte[(int) file.length()];

        try (FileInputStream inputStream = new FileInputStream(file)) {
            int bytesRead = inputStream.read(buffer);
            if (bytesRead == -1) {
                return "";
            }
            return new String(buffer, 0, bytesRead, StandardCharsets.UTF_8);
        }
    }
}
```

### What the code does
1. Creates a text payload in memory.
2. Opens a file for writing.
3. Writes bytes to disk through the OS file path.
4. Opens the same file for reading.
5. Reads the bytes back into memory.
6. Prints the content so the round trip is visible.

## Code Explanation

`FileOutputStream` and `FileInputStream` are important here because they are thin Java wrappers around native file descriptors. They still live inside the JVM, but the actual file access is performed by the operating system after the JVM crosses into native code.

`write()` does not mean “write to disk right now.” In this program, the Java call eventually becomes a kernel write request. The kernel usually copies the bytes into its page cache first, marks the page dirty, and schedules physical disk flush later. That is why file writes can return before the bytes are permanently on storage.

A successful write does not guarantee that data is physically persisted on disk. The data may remain in the kernel page cache and be flushed later. Durability is only guaranteed after an explicit sync operation such as `fsync()`.

`read()` works in the opposite direction. If the data is already in the page cache, the kernel can satisfy the read from RAM. If not, it fetches the file blocks from storage, places them in memory, and copies the bytes back into the process buffer.

If the file data is already present in the page cache, repeated reads can be served entirely from RAM without accessing disk, significantly reducing latency.

## System Call Analysis

If this program is run on a Linux system with:

```bash
strace -f java FileIOAssignment
```

For a cleaner report, I used this filtered version while testing:

```bash
strace -f -e trace=openat,read,write,close -o trace.log java FileIOAssignment
```

This command requires `java` to be installed inside the same Linux environment where `strace` is running. In WSL, `strace` can only trace a Linux `java` binary, not a Windows `java.exe` launched through interop. If `java` is missing, install a JDK in WSL first, for example:

```bash
sudo apt install default-jdk
```

Then the output would include many JVM startup calls, but the important part is the file access path. In my WSL run, the relevant lines looked like this:

```text
openat(AT_FDCWD, "assignment-data.txt", O_WRONLY|O_CREAT|O_TRUNC, 0666) = 4
write(4, "Advanced DBMS Lab 1\nThis file wa"..., 178) = 178
close(4) = 0
openat(AT_FDCWD, "assignment-data.txt", O_RDONLY) = 4
read(4, "Advanced DBMS Lab 1\nThis file wa"..., 178) = 178
close(4) = 0
```

### What each syscall means

`open()` or `openat()`
This asks the kernel to resolve the pathname, check permissions, and create an open-file entry for the process. The kernel returns a file descriptor, which is a small integer handle to that open file object. In the trace, `openat()` is common because modern libc and JVM code often use it instead of plain `open()`.

A file descriptor is an index into the process’s file descriptor table, which maps to kernel-level file objects that track the state of the open file.

`write()`
This copies bytes from user space into the kernel’s control. For regular files, the kernel usually copies the data into the page cache and marks the page dirty. The actual device write can happen later, either on flush, memory pressure, or background writeback.

`read()`
This asks the kernel to copy bytes from the file into the program’s memory. If the requested file data is already cached, the kernel returns it from RAM. Otherwise the kernel reads the relevant blocks from storage first and then copies the bytes to user space.

`close()`
This releases the file descriptor and tells the kernel the process is done with that open handle. The close itself does not mean the file contents are physically synced at that exact moment; dirty pages can still be written back by the kernel afterward unless an explicit sync is used.

## Full Internal Flow

The full journey is:

User Code -> JVM -> Native Runtime -> System Call -> Kernel -> File System -> Disk / Page Cache -> RAM -> Back to User Code

### 1. User code
The Java program calls `FileOutputStream.write()` or `FileInputStream.read()`.

### 2. JVM
The JVM handles the Java method call and reaches the native implementation behind the stream class. At this point Java is still managing objects and exceptions, but the file operation is no longer a pure Java operation.

The JVM uses native methods (via JNI) to bridge Java I/O operations to the underlying OS system calls.

### 3. Native runtime
The runtime layer performs the Java-to-native bridge. This is where the JVM uses the platform ABI and invokes the OS-facing file APIs that ultimately become syscalls.

### 4. System call boundary
At this point the process crosses from user space into kernel space. This is the key boundary because user programs are not allowed to touch hardware directly.

### 5. Kernel
The kernel validates the request, resolves the file path, checks permissions, and manages buffers, caches, and metadata. For a regular file write, the kernel usually copies data into the page cache rather than pushing it straight to the disk on the same instruction.

### 6. File system
The file system maps the logical file name to inode metadata and the file’s data blocks or extents. The inode stores metadata and the pointers or extent references needed to locate the data; the directory entry stores the name-to-inode mapping.

### 7. Disk / page cache
If the data is already cached, the kernel serves it from RAM. If not, the storage stack issues block I/O to the disk controller, often using DMA so the device can transfer data directly into memory without the CPU copying each byte.

### 8. Back to user code
The result is copied back into the program’s memory, and the Java code sees the data as if it came directly from the file.

## Core Concepts

### File Descriptor
A file descriptor is a small integer returned by the kernel after a file is opened. It is like a ticket number for the open file. The process uses that number for read, write, and close operations.

### inode
An inode stores metadata about a file, not the file name itself. It typically contains ownership, permissions, timestamps, file size, link count, and pointers or extent references to the data blocks. The directory entry maps the human-readable name to the inode number, so the name lives in the directory, not in the inode.

### System Calls
System calls are the controlled entry points from user space into the kernel. They are needed because only the kernel has the privilege to access hardware, manage memory safely, and protect one process from another.

### strace
`strace` traces the system calls made by a process. It is useful because it shows the real OS-level behavior instead of just the high-level Java code. For this assignment, it proves that file I/O eventually becomes kernel work.

### RAM vs Disk
RAM is fast but volatile, so its contents disappear when power is off. Disk is much slower but persistent, so it keeps data even after shutdown. Databases need both: RAM for hot pages and working state, disk for durability and recovery.

### Buffered I/O
Buffered I/O means data is accumulated before it reaches the final destination. In this lab, the more important buffering is actually kernel buffering: the kernel keeps file pages in the page cache and writes them out later. That reduces disk traffic, but it also means a successful `write()` is not the same thing as durable storage unless the data is flushed or synced.

### Blocks vs Pages
A block is a storage/file-system unit, while a page is a memory-management unit, usually 4 KB on many systems. They are related because the kernel moves file data between disk blocks and memory pages, often using the page cache as the bridge.

### Disk Drivers
Disk drivers are kernel components that know how to communicate with storage hardware. The application never talks to the disk directly. The driver translates the kernel’s generic block requests into device-specific commands and handles completion interrupts or polling.

### DMA
Direct Memory Access lets the storage controller transfer data between the device and RAM without the CPU copying every byte itself. The CPU still sets up the transfer, and the kernel still coordinates the I/O path, but the bulk data movement is handled by the device and memory subsystem.

### Why the CPU is bypassed
The CPU is not used as the byte-by-byte mover because that would waste processing time and bus bandwidth. Instead, it programs the I/O request, and DMA plus the storage controller move the data. That is faster and frees the CPU to do other work while the transfer is in flight.

## Database Connection

Databases need disk because they must keep data after the process exits or the machine powers off. RAM alone cannot provide persistence, so durable state must eventually be written to non-volatile storage.

Databases use RAM because access is dramatically faster than disk access. This is why database systems keep hot pages in memory and try to avoid disk reads during query execution.

The OS page cache plays a role similar to a database buffer pool by caching frequently accessed data in memory to reduce disk I/O. However, databases often implement their own buffer management to gain finer control over caching and consistency.

This leads directly to the main DBMS components and the same basic idea as the OS page cache:

`Buffer pool` keeps frequently used pages in memory and reduces physical I/O.

`Storage engine` manages how records are stored, fetched, updated, and recovered on disk.

`Query execution` decides which pages need to be touched and tries to minimize expensive disk access.

So this simple file program is a small version of the same storage path a database depends on: request data, consult memory first when possible, and fall back to disk only when needed.

## Sample Output

```text
File written successfully: .../assignment-data.txt

Read back from file:

Advanced DBMS Lab 1
This file was written using FileOutputStream and read using FileInputStream.
It demonstrates how user code reaches the operating system through system calls.
```

## Learnings

This lab made the file path much clearer. A program does not magically read or write storage. It asks the JVM, which asks the kernel, which coordinates metadata, memory, and hardware. The same basic path is what makes database persistence possible.

## Submission Contents

* `FileIOAssignment.java`
* `README.md`
* `trace.log`
