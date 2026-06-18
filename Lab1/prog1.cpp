#include <unistd.h>
#include <fcntl.h>
#include <iostream>

int main(){
    int fd = open("test.txt",O_RDWR,0666);
    if(fd==-1){
        std::cout<<"couldnt open";
        return 0;
    }
    char buffer[10];
    ssize_t bytesread = read(fd,buffer,sizeof(buffer)-1);
    buffer[bytesread] = '\0';
    std::cout<<buffer;
    return 0;
}