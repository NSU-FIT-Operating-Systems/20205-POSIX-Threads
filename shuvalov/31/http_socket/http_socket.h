#include <stdio.h>
#include <netinet/in.h>
#include "picohttpparser/picohttpparser.h"

#ifndef INC_31_HTTP_SOCKET_H
#define INC_31_HTTP_SOCKET_H


struct request {
    char* buf, * method, * path;
    struct phr_header headers[100];
    size_t buf_size, buf_len, prev_buf_len, method_len, path_len, num_headers;
    int minor_version;
};

struct response {
    struct client* clients[100];
    char* buf, * message;
    struct phr_header headers[100];
    size_t buf_size, buf_len, prev_buf_len, message_len, num_headers;
    int minor_version, status, clients_num, content_length, not_content_length;
};

struct client {
    struct response* response;
    struct request request;
    size_t poll_index;
    ssize_t bytes_written;
    int fd, processed;
};

struct server {
    struct request* request;
    struct response* response;
    struct sockaddr_in serv_addr;
    size_t poll_index;
    ssize_t bytes_written;
    int fd, processed;
};

void setup_client(int index, struct client* clients);

int setup_server(int index, struct server* servers);

void free_clients(struct client* clients, size_t servers_num);

int add_fd_to_clients(int fd, size_t poll_index, struct client* clients, size_t clients_size);

int add_fd_to_servers(int fd, struct sockaddr_in serv_addr,
                      size_t poll_index, struct request* request,
                      struct server* servers, size_t servers_size);

ssize_t get_header_value(char** value, size_t* value_len, char* header_name,
                         struct phr_header* headers, size_t num_headers);

#endif //INC_31_HTTP_SOCKET_H