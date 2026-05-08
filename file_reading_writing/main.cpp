#include <fcntl.h>    // open(), O_RDONLY, O_WRONLY, O_CREAT, O_TRUNC
#include <sys/stat.h> // fstat(), struct stat
#include <unistd.h>   // read(), write(), close()

int main() {

  // ── WRITE ────────────────────────────────────────────────────────────────
  // open() returns a file descriptor (fd) — just an integer the kernel gives
  // us as a handle to the file. 0=stdin, 1=stdout, 2=stderr are reserved,
  // so our fd starts at 3.
  int write_fd = open("test.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (write_fd == -1) {
    write(2, "ERROR: cannot open for writing\n", 31);
    return 1;
  }

  const char *msg = "Yoooooooo!\n";
  write(write_fd, msg, 11); // write bytes to the file
  close(write_fd);          // release the fd back to the kernel

  // ── READ ─────────────────────────────────────────────────────────────────
  int read_fd = open("test.txt", O_RDONLY);
  if (read_fd == -1) {
    write(2, "ERROR: cannot open for reading\n", 31);
    return 1;
  }

  char buf[128];
  int n = read(read_fd, buf, sizeof(buf)); // kernel copies bytes into buf

  // ── INODE ────────────────────────────────────────────────────────────────
  // Every file on disk has an inode: a kernel data structure that stores
  // metadata (size, permissions, timestamps, disk-block pointers).
  // fstat() gives us that metadata for an open fd.
  struct stat info;
  fstat(read_fd, &info); // info.st_ino = inode number of test.txt
  close(read_fd);

  // ── PRINT RESULTS ────────────────────────────────────────────────────────
  write(1, "Contents : ", 11);
  write(1, buf, n);

  // convert inode number to a string so we can write() it
  write(1, "Inode    : ", 11);
  char tmp[32];
  unsigned long ino = info.st_ino;
  int i = 30;
  tmp[31] = '\n';
  do {
    tmp[i--] = '0' + (ino % 10);
    ino /= 10;
  } while (ino);
  write(1, tmp + i + 1, 30 - i + 1);

  return 0;
}
