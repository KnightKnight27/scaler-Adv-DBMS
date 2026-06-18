// #include <bits/stdc++.h> // This includes standard libraries, but we need POSIX standard libs.

#include <iostream>
#include <fcntl.h>      // For open()
#include <unistd.h>     // For read(), write(), close()
#include <sys/stat.h>
#include <cstring>

using namespace std;
#define endl "\n" // To avoid writing \n repeatedly, endl is muscle memory :)

int main() 
{
    // ==============
    // 1. OPEN FILE
    // ==============
    int fileDesc = open( // Limited int number for file descriptor with different meanings
        "file1.txt", 
        O_RDWR | O_CREAT, // Bit flags to control how file is opened, | to combine the bits
        0644 // File permissions, only when creating file, 0 for octal
    );

    if(fileDesc < 0) // OS returns -1 on failure
    {
        cerr << "Failed to open file" << endl; // cerr for throwing errors
        return -1;
    }

    cout << "File Descriptor : " << fileDesc << endl;

    // Compile: g++ main.cpp -std=c++17 -o app
    // -std flag to set the version, -o to set custom name of output file





    //===================
    // 2. WRITE TO FILE
    //===================
    const char* firstMessage = "First Message!\n";
    // Why not string? Because string is a container which kernel can't understand, const char* is a pointer directly pointing to the string in memory, and kernel wants this address.
    // write() is a syscall so it can't take string directly
    // If you still want to send a string, use msg.c_str(), it converts string to const char*

    ssize_t bytesWritten = write( // This will overwrite data as current offset (starting point) is 0 (start)
        fileDesc, 
        firstMessage, 
        strlen(firstMessage) // Returns how many bytes, but in the datatype that write() needs, to tell kernel how many bytes to write
    );
    // Now offset has changed, if you write again it will be from there.

    if(bytesWritten < 0) // This is why we used "ssize_t", means signed size type and can return positive or negative on failure
    {
        cerr << "Could not write" << endl;
        close(fileDesc);
        return -1;
    }

    cout << "Bytes Written : " << bytesWritten << endl;

    string secondMessage = "Second message ?";

    ssize_t bytesAnother = write(
        fileDesc, 
        secondMessage.c_str(), 
        strlen(secondMessage.c_str())
    );

    if(bytesAnother < 0) 
    {
        cerr << "Failed" << endl;
        close(fileDesc);
        return -1;
    }

    cout << "Wrote again : " << bytesAnother << endl;
    // See, our new data was on line 2, due to "\n" in previous write ops.





    // ==========================
    // 3. Moving the file offset 
    // =========================

    lseek(
        fileDesc, // Which file to change
        0, // How much
        SEEK_SET // From where
    );
    // It will move 0 from starting (SEEK_SET) for our "fileDesc" => starting offset, let's go

    // Now try write or read from starting. 





    // =============
    // 4. READ FILE 
    // =============
    
    char buffer[1024]; 
    // Char array as read() works with bytes not strings
    // So we gave OS a memory area to store upcoming data

    ssize_t bytesRead = read(
        fileDesc, // Our file descriptor
        buffer, // Where to store
        sizeof(buffer) - 1 // Max bytes to store, -1 as we reserved one byte for null terminator
    );

    if(bytesRead < 0)
    {
        cerr << "Failed to read" << endl;
        close(fileDesc);
        return -1;
    }

    buffer[bytesRead] = '\0'; // Null terminator, to inform cout where to stop printing, otherwise garbage values might be printed.

    cout << "\nContent Read from File: " << endl;
    cout << buffer << endl; // Benefit of char array, can print whole contents directly like this





    // =============
    // 5. CLOSE FILE
    // =============

    close(fileDesc); // Closing the file descriptor is necessary to prevent memory leaks

    return 411; 
}