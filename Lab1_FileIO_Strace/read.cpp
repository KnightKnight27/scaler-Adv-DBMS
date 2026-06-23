#include <unistd.h>
#include <sys/syscall.h>
#include <fcntl.h>

int main(){
	char buffer[100];
	
	int fd = syscall(SYS_openat, AT_FDCWD, "test.txt", O_RDONLY, 0);
	
	int bytes = syscall(SYS_read, fd, buffer, sizeof(buffer));
	
	syscall(SYS_write, 1, buffer, bytes);
	
	syscall(SYS_close, fd);

	return 0;
}
