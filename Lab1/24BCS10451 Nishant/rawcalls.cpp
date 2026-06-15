#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

constexpr const char *kInputPath = "input.txt";
constexpr const char *kOutputPath = "output.txt";
constexpr size_t kBufferSize = 2048;

ssize_t writeFully(int fd, const char *data, size_t length) {
  size_t totalWritten = 0;
  while (totalWritten < length) {
    ssize_t bytesWritten =
        write(fd, data + totalWritten, length - totalWritten);
    if (bytesWritten == 0) {
      errno = EIO;
      return -1;
    }
    if (bytesWritten < 0) {
      if (errno == EINTR) {
        continue;
      }
      return -1;
    }
    totalWritten += static_cast<size_t>(bytesWritten);
  }
  return static_cast<ssize_t>(totalWritten);
}

void printInfo(const char *message) {
  writeFully(STDOUT_FILENO, message, std::strlen(message));
}

void printError(const char *context) {
  writeFully(STDERR_FILENO, context, std::strlen(context));
  writeFully(STDERR_FILENO, ": ", 2);
  const char *errorText = std::strerror(errno);
  writeFully(STDERR_FILENO, errorText, std::strlen(errorText));
  writeFully(STDERR_FILENO, "\n", 1);
}

int main() {
  int seedFd = open(kInputPath, O_WRONLY | O_CREAT | O_EXCL, 0644);
  if (seedFd >= 0) {
    const char *seedText = "Raw syscalls demo.\n"
                           "This file was seeded with write().\n";
    if (writeFully(seedFd, seedText, std::strlen(seedText)) < 0) {
      printError("seed input file");
      close(seedFd);
      return 1;
    }
    close(seedFd);
  } else if (errno != EEXIST) {
    printError("create input file");
    return 1;
  }

  int inputFd = open(kInputPath, O_RDONLY);
  if (inputFd < 0) {
    printError("open input file");
    return 1;
  }

  int outputFd = open(kOutputPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (outputFd < 0) {
    printError("open output file");
    close(inputFd);
    return 1;
  }

  char buffer[kBufferSize];
  ssize_t bytesRead = 0;
  while ((bytesRead = read(inputFd, buffer, sizeof(buffer))) > 0) {
    if (writeFully(outputFd, buffer, static_cast<size_t>(bytesRead)) < 0) {
      printError("write output file");
      close(inputFd);
      close(outputFd);
      return 1;
    }
  }

  if (bytesRead < 0) {
    printError("read input file");
    close(inputFd);
    close(outputFd);
    return 1;
  }

  close(inputFd);
  close(outputFd);
  printInfo("Copied from input.txt to output.txt using system calls.\n");
  return 0;
}
