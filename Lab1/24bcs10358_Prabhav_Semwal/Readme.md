# Goal
Learn how opening, reading and writing works through raw system calls using C++ 

# Studying terminology
I stumbled on following terminology:
1. System calls: It is sent by user programs to kernel to do things that user programs cannot do directly. In OS user programs cannot interacte directly with hardware or critical OS resource as this can result in instability or security issues. Hence user programs issue system calls to kernel which then executes them. (It also provide a common interface to interact with hardware or else every program will have to write its own driver)
2. File Descriptors: It is an integer handle to an open resource. In UNIX everything is a file so everyhting has File Descriptor. Well it is more like linux kernel abstracted everything as a stream so we can have same read/write syscalls for them.
3. INODE: It is a metadata of file created by Linux. It stores everything about the file except the filename and data.
4. Block: It is a unit of storage in the Disk
5. Page: It is a unit of memory (Ram)
6. Strace: It is a cmdline tool that helps us view the raw system calls a program makes.
7. DMA: It stands for Direct Memory Access, is an hardware mechanism that let's device trasfer data directly from Ram without CPU comming in between to copy each byte.It uses DMA Controller an hardware component that is inside CPU , and nowadays in devices itself, OS sends its the instruction and it does the copying on its own without CPU comming in between and when it ends it sends an raises an interrupt to the CPU. Fun Fact: Network card uses DMA to directly write the data packets into
   RAM.

# Coding it Up
I have created 2 C++ programs that use systemcalls to Create, Write and Read a file using different level of abstraction main.cpp uses systemcall wrappers like open, read, write from <unistd.h> and main_raw.cpp uses syscall() directly (in actuality it is also a thin wrapper but it is the best we can get without directly using inline assembly)

