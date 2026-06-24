# Research Report: Reading and Writing Files with System Calls

## Introduction

In this project, I wrote a C++ program that uses direct Windows system calls to read and write a file instead of using the standard C++ libraries like `<iostream>` or `<fstream>`. By doing this, we can see exactly how the operating system handles file operations.

## Pain Points of Raw System Calls

Using system calls directly gives us more control, but it creates a few major problems:

1. Buffer Management: When you use standard libraries, they automatically handle memory buffers for you. With raw system calls, you have to guess exactly how much memory you need before you read the data. If you guess wrong, your program might crash or cut off the text.
2. Error Handling: Standard libraries give you clear error messages or exceptions. System calls only give back simple numbers or true and false values. This makes it really hard to figure out what went wrong when a file fails to open.
3. Code Complexity: A simple task like printing to the screen takes several lines of complicated code. You have to ask the operating system for permission for every little thing.

## Solutions to These Problems

To fix these issues in my code, I used a few simple solutions:

1. Fixed Buffer Sizes: I created a character array of a specific size (100 bytes) and told the system exactly how much it was allowed to read. This stops the memory from overflowing.
2. Manual Checks: I added "if" statements after every single system call. If the system returned an invalid handle or a false result, I told the program to close the file and stop running safely.
3. Standard Output Handling: To print the text, I asked the system for a special console handle using `GetStdHandle`. Then I treated the console just like a regular file and wrote my text directly to it.

## Implementation Steps

Here is the step by step process of how the program works.

### Step 1: Creating the File

First, I used the `CreateFileA` function from the Windows API. This tells the OS to create a file named `test_output.txt` or open it if it already exists. I had to specify `GENERIC_WRITE` so the program gets permission to write to it. If it works, the OS gives back a handle. A handle is basically a tracking number for the open file.

### Step 2: Writing Data

Next, I created a text string to write to the file. I used the `WriteFile` function and passed it the handle I got from `CreateFileA`, the text string, and the length of the string. The OS takes this data and writes it directly to the disk.

### Step 3: Closing the Write Handle

After writing, it is important to release the file so other programs can use it. I used `CloseHandle` to tell the OS that I was done writing.

### Step 4: Re-opening for Reading

To read the data back, I had to call `CreateFileA` again. This time, I used `GENERIC_READ` for the access rights and `OPEN_EXISTING` because I only want to read it if the file is actually there.

### Step 5: Reading the File

I made an empty array of characters to hold the data. Then I called `ReadFile`, passing the new handle and the empty buffer. The OS reads the file from the disk and puts the text into my buffer.

### Step 6: Closing the Read Handle

Just like before, I used `CloseHandle` to close the file when I finished reading.

### Step 7: Printing to the Console

Since I couldn't use `std::cout`, I had to use system calls to print the result to the console. I got the handle for the standard output using `GetStdHandle` and then used `WriteFile` to print the buffer to the screen.

## Conclusion

Overall, this assignment showed me how much work the standard libraries do for us behind the scenes by managing all these handles and buffers automatically.
