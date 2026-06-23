#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>

int main() {
    const char* file_name = "jatin_data.txt";

    // Open file
    int file_desc = open(file_name, O_CREAT | O_RDWR, 0644);
    if (file_desc < 0) {
        perror("File opening failed");
        return 1;
    }

    // Write data
    const char* text =
        "Name: Jatin Chulet\n"
        "Roll No: 24BCS10213\n"
        "Course: B.Tech CSE\n";

    ssize_t written_bytes = write(file_desc, text, strlen(text));

    if (written_bytes < 0) {
        perror("Writing failed");
        close(file_desc);
        return 1;
    }

    // Move cursor to beginning
    lseek(file_desc, 0, SEEK_SET);

    // Read data
    char data[200];

    ssize_t read_bytes =
        read(file_desc, data, sizeof(data) - 1);

    if (read_bytes < 0) {
        perror("Reading failed");
        close(file_desc);
        return 1;
    }

    data[read_bytes] = '\0';

    std::cout << "Student Record:\n";
    std::cout << data;

    // Close file
    close(file_desc);

    return 0;
}