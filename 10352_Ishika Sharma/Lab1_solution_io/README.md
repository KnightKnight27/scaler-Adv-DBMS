
# Programming related definitions
- file descriptor (fd) - It is a index number in a file descriptor table maintained by the OS which has all entries to current open files. open() typically returns the first free such index.

- errno - It is like a special integer error code set by C functions like open(), read(), write() when something goes wrong. Linux commands like `errno <NUM>` can be used to understand these numbers better

- PAGE - It's the smallest unit of memory (in terms of files) that the OS can manage. Typically 4 KB on linux systems.

# System calls used

All these are not actual "system calls". They are C functions which are very thin wrappers around the actual kernel system calls.

- open(const char* file_path, int flags) - opens file via modes described by flags and returns a file descriptor.

- read(int fd, char* buff, int count) - reads `count` bytes from the file referred by the `fd` and stores result in `buff`.

- write(int fd, char* buff, int count) - writes `count` bytes from buff into file referred by the `fd`. The mode of the write `WRITE` or `APPEND` depends on what flag was used in open()

close(int fd) - closes the file referred by `fd` i.e. asks the OS to free the entry in the file descriptor table

# Why close() a file ?

- When you write to a file, the changes don't get written to disk immediately.
The file is written in "buffered" mode i.e. first some data gets written to a in-memory buffer and when the buffer reaches it's capacity then the buffer gets "flushed" or written to the file.
The buffer is also flushed when you call close().
So if you don't call close() then you might lose some data that was stored in the buffer which didn't get flushed.

- The OS must have a limited size file descriptor table. In that case, resources are being wasted if you don't close a file after you have no use for it.
Although the fd index would obviously be freed when the process finishes.

# Why a disk read is expensive compared to reading from RAM storages
Disks are supposed to be persistent i.e. data is not lost when the system powers off.<br>
Doing something like that is not possible by entirely using circuits as circuits operate on voltages and the voltage obviously drops to null when power is gone.<br>
Data managed through circuits simply won't remain.<br>
So persistent disks are typically implemented through concepts of "magnetizing a material"<br>
HDDs in particular are like a circular slate with millions of very tiny materials which could be magentized (1) and degmagnetized (0)<br>
Once a tiny block is magentized, it stays in that state unless degmagnetized (and vice versa), and thus persistance is maintained.<br>
The above is true disregarding decay in magnetization since I think the time period is very long.<br>
Every time some data needs to be accessed, HDDs have a marker which rotates to the right data section and thus that is a physical action which is much slower than the magic that happens in circuits.

# How slower are disk reads & writes compared to RAM reads & writes
According to https://queue.acm.org/detail.cfm?id=1563874<br>
Memory access vs Disk access (sequential) => memory is 6x faster<br>
Memory access vs Disk access (random access) => memory is around 100,000x faster<br>

# What happens when a file is read?

- First a file descriptor is created in the file descriptor table on the open() call.<br>
This fd is used in all further operations such as read() and write()

- Now the OS communicates with the disk driver (the program which manages the disk) and copies the needed PAGE into an area of the RAM called "page cache".<br>
Note that the calling process also needs a separate copy of data in it's process memory (either stack or heap).<br>
So the needed content (or collection of pages) must also be present in the process memory.<br>
So does this mean a simple read from disk involves copying the same data twice?<br>
Not exactly. The process actually maps it's virtual address space to the page cache similar to how `mmap` works.

- When a read() call occurs, the OS first checks the page cache for the pages, if they are present the data is directly read from memory, and thus fast.<br>
If some page is not present, that page is fetched from disk and stored in page cache, and thus slow.

- The page cache is a "cache" and thus limited in capacity.<br>
So we need some sort of an eviction policy for it.<br>
The OS manages it by default.<br>
Which means, if you need to optimize based on this page cache's eviction policy, you can't do this directly.

# What happens when a file is written to?

- First step is the same as when you read from a file. There must be an open() call made first with the appropriate flags (O_WRONLY, O_APPEND)

- The OS doesn't immediately write the data to the disk.<br>
But why?<br>
That's because disk writes are also expensive.<br>
The OS writes the data to the disk in chunks, more technically called "buffered writing".<br>
Whatever data you write can be expressed in PAGEs and thus it's first written to the page cache which is in-memory and is fast.<br>
When the OS realises that there's a need to "flush" (or write to disk) the data, it does so. And thus this process is asynchronous.<br>
The OS marks a page in the page cache as "dirty" when it's written to the page cache but not to the disk. This is so it remembers to write it in the disk eventually.<br>
There are algorithms in place that make the OS decide when to write the data to the disk but the goal is to do it in idle time.<br>
The close() call actually also flushes the data after erasing the fd entry.