#include <errno.h>
#include <unistd.h>

int main() {

	// Open file in read-only mode

	int fd = open("file.txt", O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "Error when open() : %d\n", errno);
		return 1;
	}

	char buffer[513];
	int read_bytes = read(fd, buffer, 512);
	if (read_bytes < 0) {
		fprintf(stderr, "Error when read() : %d\n", errno);
		return 1;
	}
	buffer[read_bytes] = '\0';

	// Display contents of file
	printf("%d bytes read\n", read_bytes);
	printf("%s", buffer);

	close(fd);

	// Open file in append mode
    
	fd = open("file.txt", O_WRONLY | O_APPEND);
	if (fd < 0) {
		fprintf(stderr, "Error when open() : %d\n", errno);
		return 1;
	}

	char write_data[] = "abcd\n";
	int bytes_written = write(fd, write_data, sizeof(write_data) - 1); // Don't write \0 to the file
	if (bytes_written < 0) {
		fprintf(stderr, "Error when write() : %d", errno); 
		return 1;
	}

	printf("%d bytes written", bytes_written);

	close(fd);
}
