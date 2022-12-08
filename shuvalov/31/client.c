#include <fcntl.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "socket_operations/socket_operations.h"

#define SA struct sockaddr
#define PORT 12000

extern int errno;

int main(int argc, char* argv[]) {
    size_t buf_len = 4096;
    char buf[buf_len];
    int fd;
    fd = open("test.txt", O_RDONLY);
    ssize_t bytes = read_all(fd, buf, buf_len);
    close(fd);

    int sock_fd;
    struct sockaddr_in servaddr;

    // socket create and verification
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd == -1) {
        printf("socket creation failed...\n");
        return EXIT_FAILURE;
    } else
        printf("Socket successfully created..\n");
    bzero(&servaddr, sizeof(servaddr));

    // assign IP, PORT
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    servaddr.sin_port = htons(PORT);

    // connect the client socket to server socket
    if (connect(sock_fd, (SA*) &servaddr, sizeof(servaddr)) != 0) {
        printf("connection with the server failed...\n");
        close(sock_fd);
        return EXIT_FAILURE;
    } else {
        printf("connected to the server..\n");
    }

    printf("Request:\n%.*s\n", (int) bytes, buf);
    write_all(sock_fd, buf, bytes);
    size_t receive_buf_len = 1024;
    char* receive_buf = (char*) malloc(sizeof(char) * receive_buf_len);
    ssize_t bytes_read = read_all(sock_fd, receive_buf, receive_buf_len);
    printf("Bytes read: %zd\nResponse:\n%.*s\n\n", bytes_read, (int) bytes_read, receive_buf);
    close(sock_fd);
    free(receive_buf);
    return EXIT_SUCCESS;
}
