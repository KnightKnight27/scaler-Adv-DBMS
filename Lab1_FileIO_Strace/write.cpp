#include <unistd.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include <cstring>

int main(){
	const char *filename = "test.txt";
	const char *data = "Hello from raw syscall!\n";
	
	int fd = syscall(SYS_openat, AT_FDCWD, filename, O_CREAT | O_WRONLY, 0644);
	syscall(SYS_write, fd, data, strlen(data));
	syscall(SYS_close, fd);
	
	return 0;
}
