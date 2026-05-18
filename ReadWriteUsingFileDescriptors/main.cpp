#include <unistd.h>
#include <fcntl.h>     
#include <iostream>
using namespace std;
int main()
{
    int fd = open("file.txt", O_RDWR);
    if (fd == -1) 
    {
        cerr << "Failed to open file.txt" << endl;
        return 1;
    }
    cout << "File opened successfully with file descriptor: " << fd << endl;

    char buffer[100];
    ssize_t bytesRead = read(fd, buffer, sizeof(buffer));
    if (bytesRead == -1) 
    {
        cerr << "Failed to read from file.txt" << endl;
        return 1;
    }
    cout << "Data read from file: " << endl;
    cout << buffer << endl;

    const char* newData = "New data to write to the file.";
    ssize_t bytesWritten = write(fd, newData, strlen(newData));
    if (bytesWritten == -1) 
    {
        cerr << "Failed to write to file.txt" << endl;
        return 1;
    }
    cout << "Data written to file successfully." << endl;

    // Move the file pointer back to the beginning of the file to read the updated content
    lseek(fd, 0, SEEK_SET);

    ssize_t bytesReadAfterWrite = read(fd, buffer, sizeof(buffer));
    if (bytesReadAfterWrite == -1)
    {
        cerr << "Failed to read from file.txt after writing" << endl;
        return 1;
    }
    cout << "Data read from file after writing: " << endl;
    cout << buffer << endl;

    if(close(fd) == -1)
    {
        cerr << "Failed to close file descriptor" << endl;
        return 1;
    }

    cout << "File descriptor closed successfully." << endl;
    return 0;
}