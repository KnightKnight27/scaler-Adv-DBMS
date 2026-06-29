#include <cstdio>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

int main() {

	// For reading

	int fd = open("file.txt", O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "Error when open() : %d\n", errno);
		return 1;
	}

	char buff[513];
	int bytes_read = read(fd, buff, 512);
	if (bytes_read < 0) {
		fprintf(stderr, "Error when read() : %d\n", errno);
		return 1;
	}
	buff[bytes_read] = '\0';

	printf("%d bytes read\n", bytes_read);
	printf("%s", buff);

	close(fd);

	// For writing

	fd = open("file.txt", O_WRONLY | O_APPEND);
	if (fd < 0) {
		fprintf(stderr, "Error when open() : %d\n", errno);
		return 1;
	}

	char to_write[] = "abcd\n";
	int bytes_written = write(fd, to_write, sizeof(to_write) - 1); // Don't write \0 to the file
	if (bytes_written < 0) {
		fprintf(stderr, "Error when write() : %d", errno); 
		return 1;
	}

	printf("%d bytes written", bytes_written);

	close(fd);
}