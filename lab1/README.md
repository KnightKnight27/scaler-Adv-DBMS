# Lab 1: File Handling Using System Calls in Linux

**Student:** Talin Daga (24bcs10321)

## Aim
Perform file I/O using low-level Linux system calls: `open`, `read`, `write`, and `close`.

## Files
| File | Description |
|------|-------------|
| `lab1_file_handling.c` | Main C program |
| `file.txt` | Sample input file read by the program |

## Build
```bash
gcc -Wall -Wextra -o lab1_file_handling lab1_file_handling.c
```

## Run
```bash
./lab1_file_handling
```

> **Note:** The program must be run from the `lab1/` directory so it can find `file.txt`.
> Each run appends a line to `file.txt` — this is expected behaviour.

## What the program does
1. Opens `file.txt` read-only (`O_RDONLY`) and prints its contents + byte count.
2. Closes the file.
3. Reopens `file.txt` in append mode (`O_WRONLY | O_APPEND | O_CREAT`) to preserve existing data.
4. Appends a fixed string using `write()` and prints the bytes written.
5. Closes the file again.
6. Checks the return value of every system call and reports errors via `perror()`.
