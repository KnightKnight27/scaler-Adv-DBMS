#include <fcntl.h>    // For open() and O_RDONLY, O_WRONLY, O_CREAT
#include <unistd.h>   // For read(), write(), close()
#include <sys/stat.h> // For file permissions

int main() {
    const char* input_file = "input.txt";
    const char* output_file = "output.txt";
    char buffer[1024];

    // 1. Open the input file for reading
    // System Call: open
    int fd_in = open(input_file, O_RDONLY);
    if (fd_in == -1) {
        // We can't use perror or cerr, so we just exit
        return 1;
    }

    // 2. Read from the input file
    // System Call: read
    ssize_t bytes_read = read(fd_in, buffer, sizeof(buffer));
    if (bytes_read == -1) {
        close(fd_in);
        return 1;
    }

    // 3. Open (or create) the output file for writing
    // System Call: open
    // O_WRONLY: Write only
    // O_CREAT: Create if it doesn't exist
    // O_TRUNC: Truncate to zero length if it exists
    // 0644: Permissions (rw-r--r--)
    int fd_out = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_out == -1) {
        close(fd_in);
        return 1;
    }

    // 4. Write to the output file
    // System Call: write
    ssize_t bytes_written = write(fd_out, buffer, bytes_read);
    if (bytes_written == -1) {
        close(fd_in);
        close(fd_out);
        return 1;
    }

    // 5. Close both files
    // System Call: close
    close(fd_in);
    close(fd_out);

    // Optional: Write a success message to standard output (FD 1)
    const char* msg = "File operation completed successfully using raw system calls.\n";
    write(1, msg, 63);

    return 0;
}
