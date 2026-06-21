#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>

int main() {
  // open file for reading

  int fd = open("file.txt", O_RDONLY);

  if (fd < 0) {
    fprintf(stderr, "open() failed : %d\n", errno);
    return 1;
  }

  // reading from file

  char buff[512];

  ssize_t bytes_read;
  ssize_t total_read = 0;

  printf("Content:\n");

  while ((bytes_read = read(fd, buff, sizeof(buff))) > 0) {
    total_read += bytes_read;

    ssize_t total_written = 0;

    // handle partial writes to stdout

    while (total_written < bytes_read) {
      ssize_t written = write(STDOUT_FILENO, buff + total_written,
                              bytes_read - total_written);

      if (written < 0) {
        fprintf(stderr, "write() failed : %d\n", errno);

        close(fd);

        return 1;
      }

      total_written += written;
    }
  }

  if (bytes_read < 0) {
    fprintf(stderr, "read() failed : %d\n", errno);
    close(fd);
    return 1;
  }

  printf("\n\nTotal %zd bytes read\n", total_read);

  // closing after reading

  close(fd);

  // opening for writing

  fd = open("file.txt", O_WRONLY | O_APPEND | O_CREAT, 0644);

  if (fd < 0) {
    fprintf(stderr, "open() for writing failed : %d\n", errno);
    return 1;
  }

  // writing to file

  const char to_write[] = "abcd\n";

  ssize_t total_written = 0;
  ssize_t len = sizeof(to_write) - 1;

  while (total_written < len) {
    ssize_t bytes_written =
        write(fd, to_write + total_written, len - total_written);

    if (bytes_written < 0) {
      fprintf(stderr, "write() failed : %d\n", errno);
      close(fd);
      return 1;
    }

    total_written += bytes_written;
  }

  printf("%zd bytes written\n", total_written);

  // closing after writing

  close(fd);

  return 0;
}
