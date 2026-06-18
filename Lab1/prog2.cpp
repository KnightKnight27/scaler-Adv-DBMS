#include <fcntl.h>
#include <iostream>
#include <unistd.h>
#include <cstring>
int main(){
    int fd = open("test.txt",O_WRONLY,0666);
    if(fd==-1){
        std::cout<<"file not found";
        return 0;
    }
    const char* msg = "her";
    write(fd,msg, strlen(msg));
    return 0;
}