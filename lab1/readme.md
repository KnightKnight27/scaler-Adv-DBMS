# Lab-1 File Handling Using Linux System Calls

## Aim

To perform basic file operations using Linux system calls such as `open()`, `write()`, `read()`, `lseek()`, and `close()` in C++.

---

## Description

This program demonstrates low-level file handling in Linux using POSIX system calls. It creates a file, writes student information into it, moves the file pointer to the beginning, reads the contents back, and displays them on the terminal.

---

## System Calls Used

### 1. open()

Opens an existing file or creates a new file.

```cpp
int file_desc = open(file_name, O_CREAT | O_RDWR, 0644);
```

### 2. write()

Writes data to the file.

```cpp
write(file_desc, text, strlen(text));
```

### 3. lseek()

Moves the file pointer to a specified location.

```cpp
lseek(file_desc, 0, SEEK_SET);
```

### 4. read()

Reads data from the file into a buffer.

```cpp
read(file_desc, data, sizeof(data) - 1);
```

### 5. close()

Closes the file and releases resources.

```cpp
close(file_desc);
```

---

## Algorithm

1. Create or open a file named `jatin_data.txt`.
2. Write student information to the file.
3. Move the file pointer to the beginning using `lseek()`.
4. Read the file contents into a character buffer.
5. Display the contents on the screen.
6. Close the file.

---

## Compilation

```bash
g++ main.cpp -o app
```

---

## Execution

```bash
./app
```

---

## Sample Output

```text
Student Record:

Name: Jatin Chulet
Roll No: 24BCS10213
Course: B.Tech CSE
```

---

## Learning Outcomes

* Understood the concept of file descriptors.
* Learned how Linux system calls interact with files.
* Performed file creation, writing, reading, and closing operations.
* Used `lseek()` to manipulate the file offset.

---

## Conclusion

The program successfully demonstrates file handling using Linux system calls. It shows how data can be written to and read from a file using low-level POSIX APIs instead of higher-level C++ file streams.
