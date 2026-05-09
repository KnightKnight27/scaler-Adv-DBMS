## Student Details

**Name:** Aparna Singha  
**Roll No:** 24BCS10353  
**Course:** Advanced DBMS  
**Submission:** Lab 1

---

# Lab 1: File Copy Using POSIX System Calls

## 1. What this lab is about

In this lab, I tried to copy one file into another file without using normal high-level file handling like `fstream`, `cout`, or `printf` for the actual file work. The main idea was to understand what happens when a program directly asks the operating system to open, read, write, and close files.

For this, I used these POSIX system calls:

- `open()`
- `read()`
- `write()`
- `close()`

This made the file-copy process more clear to me because the program is not hiding everything behind C++ libraries. We get to work with file descriptors and buffers directly.

---

## 2. Files used

| File | Use |
|---|---|
| `file_copy.cpp` | Program for copying the file using POSIX calls |
| `input.txt` | Source file that has the original content |
| `output.txt` | File created/overwritten by the program |
| `README.MD` | Notes, commands, code, and observations for this lab |

---

## 3. Basic flow of the program

The program works like this:

1. Open `input.txt` in read-only mode.
2. Create `output.txt` if it does not exist, or overwrite it if it already exists.
3. Read data from the input file in small chunks.
4. Write the same chunk into the output file.
5. Repeat this until `read()` returns `0`, which means the file has ended.
6. Close both file descriptors.
7. Print how many bytes were copied and how many `read()` calls were used.

The important thing I noticed is that copying is not really happening in one direct step. Data first comes into a buffer, and then that buffer is written to the second file.

---

## 4. Source code

```cpp
// Lab 1: File copy using only POSIX system calls
// Main calls used: open, read, write, close

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>

static const char *INPUT_FILE = "input.txt";
static const char *OUTPUT_FILE = "output.txt";
static const size_t BUFFER_SIZE = 4096;

static int write_all(int fd, const char *buffer, size_t size) {
    size_t written_total = 0;

    while (written_total < size) {
        ssize_t written = write(fd, buffer + written_total, size - written_total);

        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }

        written_total += (size_t)written;
    }

    return 0;
}

static void print_text(int fd, const char *text) {
    write_all(fd, text, strlen(text));
}

static char *number_to_text(unsigned long long value, char *end) {
    *--end = '\0';

    if (value == 0) {
        *--end = '0';
        return end;
    }

    while (value > 0) {
        *--end = '0' + (value % 10);
        value /= 10;
    }

    return end;
}

int main() {
    int input_fd = open(INPUT_FILE, O_RDONLY);

    if (input_fd < 0) {
        print_text(STDERR_FILENO, "Error: unable to open input.txt\n");
        return 1;
    }

    int output_fd = open(OUTPUT_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);

    if (output_fd < 0) {
        print_text(STDERR_FILENO, "Error: unable to create output.txt\n");
        close(input_fd);
        return 1;
    }

    char buffer[BUFFER_SIZE];
    unsigned long long total_bytes = 0;
    unsigned long long read_count = 0;

    ssize_t bytes_read;

    while ((bytes_read = read(input_fd, buffer, sizeof(buffer))) > 0) {
        if (write_all(output_fd, buffer, (size_t)bytes_read) < 0) {
            print_text(STDERR_FILENO, "Error: write failed\n");
            close(input_fd);
            close(output_fd);
            return 1;
        }

        total_bytes += (unsigned long long)bytes_read;
        read_count++;
    }

    if (bytes_read < 0) {
        print_text(STDERR_FILENO, "Error: read failed\n");
        close(input_fd);
        close(output_fd);
        return 1;
    }

    close(input_fd);
    close(output_fd);

    char bytes_buffer[32];
    char reads_buffer[32];

    char *bytes_text = number_to_text(total_bytes, bytes_buffer + sizeof(bytes_buffer));
    char *reads_text = number_to_text(read_count, reads_buffer + sizeof(reads_buffer));

    print_text(STDOUT_FILENO, "Copied ");
    print_text(STDOUT_FILENO, bytes_text);
    print_text(STDOUT_FILENO, " bytes using ");
    print_text(STDOUT_FILENO, reads_text);
    print_text(STDOUT_FILENO, " read() calls\n");

    return 0;
}
```

---

## 5. Commands used

```bash
g++ -std=c++17 -O2 -Wall -Wextra file_copy.cpp -o file_copy
./file_copy
```

Output I got:

```text
Copied 5623 bytes using 2 read() calls
```

The number can change if the content of `input.txt` changes or if the buffer size is changed.

---

## 6. How I checked the output

To make sure the copy was correct, I compared the file sizes and hashes.

### Linux/macOS

```bash
wc -c input.txt output.txt
md5 input.txt output.txt
```

### Windows PowerShell

```powershell
Get-Item input.txt, output.txt | Select-Object Name, Length
Get-FileHash input.txt -Algorithm MD5
Get-FileHash output.txt -Algorithm MD5
```

If both files show the same size and same hash, then the copied file is the same as the original file.

---

## 7. Concepts I understood

### File descriptor

When `open()` is called, the operating system returns a small integer. That number is the file descriptor. The program then uses this number in `read()`, `write()`, and `close()` instead of directly using the filename again and again.

Common file descriptors are:

- `0` for standard input
- `1` for standard output
- `2` for standard error

Newly opened files usually start from `3`.

### `read()`

`read()` takes bytes from the file descriptor and puts them into a buffer. If it returns a positive number, that many bytes were read. If it returns `0`, the file has ended. If it returns a negative value, something went wrong.

### `write()`

`write()` sends bytes from memory to a file descriptor. One thing I learned here is that `write()` may not always write all bytes in one call, so the program uses `write_all()` to keep writing until the full buffer is written.

### Page cache

The OS does not always read directly from disk every time. It can keep recently used file data in RAM using the page cache. Because of this, reading the same file again can be faster.

### Inode

An inode stores file metadata like size, permissions, timestamps, owner details, and information about where the file data is stored. The filename is not stored inside the inode directly. The directory maps the filename to the inode.

### DMA

DMA means Direct Memory Access. It allows storage devices to transfer data directly to RAM without making the CPU copy every byte manually. This is one reason modern I/O can be faster.

---

## 8. My final understanding

Before this lab, I mostly thought of file copying as something simple like reading from one file and writing to another using a library. After doing it with system calls, I understood that high-level file functions are built on top of lower-level OS operations.

This lab helped me connect file handling with file descriptors, buffers, page cache, inodes, and disk I/O. The main practical point I learned is that even a simple file-copy program needs proper error handling and proper handling of partial writes.
