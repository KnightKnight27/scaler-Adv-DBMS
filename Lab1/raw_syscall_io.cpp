#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>

static const char* TARGET_FILE = "file.txt";
static const int READ_LIMIT = 512;

static int open_file(const char* path, int flags) {
	int descriptor = open(path, flags);
	if (descriptor < 0) {
		fprintf(stderr, "open() failed with errno: %d\n", errno);
	}
	return descriptor;
}

int main() {
	int fd = open_file(TARGET_FILE, O_RDONLY);
	if (fd < 0) return 1;

	char buf[READ_LIMIT + 1];
	ssize_t n = read(fd, buf, READ_LIMIT);
	if (n < 0) {
		fprintf(stderr, "read() failed with errno: %d\n", errno);
		close(fd);
		return 1;
	}
	buf[n] = '\0';
	close(fd);

	printf("%zd bytes read\n", n);
	fputs(buf, stdout);

	fd = open_file(TARGET_FILE, O_WRONLY | O_APPEND);
	if (fd < 0) return 1;

	const char append_data[] = "abcd\n";
	const size_t data_len = sizeof(append_data) - 1;
	ssize_t written = write(fd, append_data, data_len);
	if (written < 0) {
		fprintf(stderr, "write() failed with errno: %d\n", errno);
		close(fd);
		return 1;
	}
	close(fd);

	printf("%zd bytes written\n", written);
	return 0;
}