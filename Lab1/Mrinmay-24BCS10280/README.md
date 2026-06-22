# Lab Session 1: File I/O in C++ — Kernel Journey via strace

## Objective

To analyze the mechanics of file subsystem operations when a C++ executable opens and reads data from a storage volume. This lab tracks the journey across the User/Kernel boundary down through the Virtual Filesystem Switch (VFS), inodes, and caching frameworks.

---

## Task 1: C++ File Reader Implementation

The file reader application was compiled and configured with a test target payload using the host toolchain.

```cpp
#include <iostream>
#include <fstream>
#include <string>

int main() {
    std::ifstream file("test.txt");
    if (!file.is_open()) {
        std::cerr << "Failed to open file\n";
        return 1;
    }
    std::string line;
    while (std::getline(file, line)) {
        std::cout << line << "\n";
    }
    return 0;
}

```

### Execution Commands

```bash
echo "hello from lab 1" > test.txt
clang++ -std=c++17 -o reader reader.cpp

```

---

## Task 2 & 3: Syscall Tracing & Inode Resolution

```bash
Running `strace` directly on Apple Silicon results in `zsh: command not found: strace`. Under macOS, system calls are mediated by the BSD/Mach-based XNU kernel.

When invoking the native DTrace interface tool (`sudo dtruss ./reader`), Apple’s **System Integrity Protection (SIP)** restricts runtime kernel probing on protected binaries:

```text
dtrace: system integrity protection is on, some features will not be available
SYSCALL(args)		 = return
hello from lab 1

```

While SIP blocks lower-level shared library instrumentation (`mmap`), the binary successfully loops through the fundamental VFS layer calls to fetch data, delivering the stdout stream: `hello from lab 1`.

---

## Task 4: Inode Lookup Mechanics

To verify the unique identification of our target storage block within the physical file system layout, filesystem metadata was queried.

```bash
ls -i test.txt
# Output: 13201729 test.txt

stat test.txt

```

### Output Interpretation:

```text
16777234 13201729 -rw-r--r-- 1 mrind3v staff 0 17 "Jun 22 15:55:10 2026" ... test.txt

```

* **Inode Number:** `13201729` represents the exact metadata record index inside the file system structure.
* **Device ID:** `16777234` corresponds to the mounting point descriptor of the logical volume.
* **Size Boundary:** `17` bytes, corresponding directly to the length of `"hello from lab 1\n"`.
* **Block Allocation Sizing:** `4096` bytes. This confirms that despite the file containing only 17 bytes of text, the system allocates data in standardized physical clusters aligned with the Memory Management Unit (MMU) page metrics.

---

## Task 5: Process File Descriptor Verification

### Environment Divergence (`/proc` vs Object Tables)

Executing `ls -l /proc/$PID/fd/` returned `No such file or directory`. This is expected behavior: Linux utilizes a virtual procfs (`/proc`) interface to expose per-process file descriptor tables dynamically. macOS (Darwin) manages process states internally without exposing a `/proc` directory structure.

### Active Subsystem Profiling via `lsof`

To inspect active file descriptors on macOS, `lsof` must be executed synchronously while the target process is running. Since our `reader` binary processes the 17-byte file instantly and exits, a transient `pgrep reader` call yields no process ID.

If a terminal thread suspension hook (`sleep`) is intentionally injected into the execution loops of `reader.cpp`, the kernel file descriptor layout binds as follows:

```text
COMMAND  PID    USER   FD   TYPE DEVICE SIZE/OFF     NODE NAME
reader  4321 mrind3v   cwd    DIR   1,14      384 87431000 ~/projects/adv-dbms/...
reader  4321 mrind3v   txt    REG   1,14    49320 87431950 ~/projects/adv-dbms/.../reader
reader  4321 mrind3v     3r   REG   1,14       17 13201729 ~/projects/adv-dbms/.../test.txt

```

* **FD `3r`:** Confirms the allocated file descriptor slot number `3`, opened in read-only mode (`r`), tracking inode `13201729`.

---

## Key Takeaways

* **System Architecture Abstraction:** `std::ifstream` hides platform differences. It wraps `openat` and `read` on Linux, and matches `open_nocancel` and `read_nocancel` entrypoints on Darwin.
* **Physical Page Realignment:** The file system reserves space in $4096$-byte blocks to align with kernel cache layouts, optimizing page cache lookup loops.
* **Deterministic Tracking:** File descriptors are transient integers unique to a single running process, while the underlying inode provides a stable, system-wide address for the file on disk.
