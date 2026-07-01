# File I/O with System calls in C++

How do we save data persistantly? ...We have disk! We can write data onto disk to store it permanantly.  
Saving Data on disk isnt that hard, we already have a lot of libraries for the same in every language.

But doing it with raw system calls like `open()` is interesting.

System calls are like requesting the OS to do an action for you, why?... because OS is the gateway between applications and the hardware beneath it.

Kernel plays an important role with respect to the access to the low level components. Kernel is what maintains something called as a `Page Cache`. It is a cache of Page which is store in the disk.

A process can only change something in the page cache `(unless explicitly mentioned to bypass the kernel)`. Kernel will periodically write the changes from the page cache into actual disk making it persistant.

So an `open()` syscall will give you back an...integer? uhm yeah that is called the file descriptor.
Everything in linux is a file. Even the network sockets. A File Descriptor is like a key to a resource.

In standard POSIX complient systems, the file descriptor

- 0 maps to stdin (terminal)
- 1 maps to stdout (terminal)
- 2 maps to stderr (terminal)

And every single new file or sockets are given a new file descriptor starting from integer 3. Each process will maintain its own File Descriptor table.

`open()` syscall will give you a file descriptor which will be used by the `write()` syscall. This is used to actually write the data from a buffer into the destination of the file descriptor.

But wait, if a file is recoganized via the file descriptor, where is the metadata of the file stored?

- It is stored in a special Data Structure called INODE
- It stores all the everything about the file except its contents and its name (metadata).
- 1% of the disk space is reserved for all the INODE in the disk. (Used to happen in older disk formats, modern ones dont have such limit)
- That limits the number of files which can be created irrespective of their size.

Coming back to write, it will take a file desciptor, a buffer and the size. It will read the size starting from the buffer pointer irrespective of the buffer size. So even if buffer is just 10 bytes and the size provided is 15 bytes, write will save garbage 5 bytes of data after reading the 10 byte buffer or worse can cause a segmentation fault.