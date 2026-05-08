#include <windows.h>

int main() {
    HANDLE hFile = CreateFileA(
        "test_output.txt",
        GENERIC_WRITE,
        0,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hFile == INVALID_HANDLE_VALUE) {
        return 1;
    }

    const char* data = "Hello, OS! This is a raw system call write.\n";
    DWORD bytesWritten = 0;
    DWORD dataLength = 44;

    BOOL writeResult = WriteFile(
        hFile,
        data,
        dataLength,
        &bytesWritten,
        NULL
    );

    if (!writeResult || bytesWritten != dataLength) {
        CloseHandle(hFile);
        return 1;
    }

    CloseHandle(hFile);

    hFile = CreateFileA(
        "test_output.txt",
        GENERIC_READ,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hFile == INVALID_HANDLE_VALUE) {
        return 1;
    }

    char readBuffer[100] = {0};
    DWORD bytesRead = 0;

    BOOL readResult = ReadFile(
        hFile,
        readBuffer,
        sizeof(readBuffer) - 1,
        &bytesRead,
        NULL
    );

    if (!readResult) {
        CloseHandle(hFile);
        return 1;
    }

    CloseHandle(hFile);

    HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hStdout != INVALID_HANDLE_VALUE) {
        DWORD stdoutBytesWritten = 0;
        WriteFile(
            hStdout,
            readBuffer,
            bytesRead,
            &stdoutBytesWritten,
            NULL
        );
    }

    return 0;
}
