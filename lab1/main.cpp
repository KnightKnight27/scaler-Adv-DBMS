#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

using namespace std;

int main() {
    const char* filename = "trace_demo.txt";
    const char* message = "Hello, Advanced DBMS system call journey!\n";
    size_t message_len = strlen(message);

    cout << "[C++] Step 1: Opening file '" << filename << "' for writing using open() system call...\n";
    // open() system call
    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        cerr << "[C++] Error opening file for writing!\n";
        return 1;
    }
    cout << "[C++] File descriptor returned: " << fd << "\n";

    cout << "[C++] Step 2: Writing message using write() system call...\n";
    // write() system call
    ssize_t bytes_written = write(fd, message, message_len);
    if (bytes_written < 0) {
        cerr << "[C++] Error writing to file!\n";
        close(fd);
        return 1;
    }
    cout << "[C++] Bytes written: " << bytes_written << "\n";

    cout << "[C++] Step 3: Closing file using close() system call...\n";
    // close() system call
    close(fd);

    cout << "\n[C++] Step 4: Re-opening file for reading...\n";
    fd = open(filename, O_RDONLY);
    if (fd < 0) {
        cerr << "[C++] Error opening file for reading!\n";
        return 1;
    }
    cout << "[C++] File descriptor returned: " << fd << "\n";

    char buffer[128];
    memset(buffer, 0, sizeof(buffer));

    cout << "[C++] Step 5: Reading message using read() system call...\n";
    // read() system call
    ssize_t bytes_read = read(fd, buffer, sizeof(buffer) - 1);
    if (bytes_read < 0) {
        cerr << "[C++] Error reading from file!\n";
        close(fd);
        return 1;
    }
    cout << "[C++] Bytes read: " << bytes_read << "\n";
    cout << "[C++] Content read: " << buffer;

    cout << "[C++] Step 6: Closing file...\n";
    close(fd);

    return 0;
}
