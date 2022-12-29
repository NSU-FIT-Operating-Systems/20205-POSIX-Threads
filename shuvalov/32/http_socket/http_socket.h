#include <stdio.h>
#include <netinet/in.h>
#include "picohttpparser/picohttpparser.h"

#ifndef INC_31_HTTP_SOCKET_H
#define INC_31_HTTP_SOCKET_H


struct request {
    char* buf, * method, * path;
    struct phr_header* headers;
    size_t buf_size, buf_len, prev_buf_len, method_len, path_len, num_headers, headers_max_size;
    int minor_version;
};

struct response {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    pthread_rwlock_t rwlock;
    struct client** subscribers;
    char* buf, * message;
    struct phr_header* headers;
    size_t buf_size, buf_len, prev_buf_len, message_len, num_headers, subscribers_max_size, headers_max_size;
    unsigned long subscribers_count;
    int minor_version, status, content_length, not_content_length, new_data_fd;
};

struct client {
    struct response* response;
    struct request request;
    size_t poll_index;
    ssize_t bytes_written;
    int fd, processed;
    int cache_node;
};

struct clients {
    struct client* members;
    size_t size;
    size_t current_number;
};

struct server {
    struct request* request;
    struct response* response;
    struct sockaddr_in serv_addr;
    size_t poll_index;
    ssize_t bytes_written;
    int fd, processed;
};

struct servers {
    struct server* members;
    size_t size;
    size_t current_number;
};

void free_client(struct client* client);

int init_clients(struct clients* clients, size_t size);

int init_servers(struct servers* servers, size_t size);

int parse_request(struct request* request);

int parse_response(struct response* response);

int init_client(struct client* client);

int init_clients_members(struct client* clients, size_t size);

void init_servers_members(struct server* servers, size_t size);

void setup_client(struct client* client);

void setup_server(struct server* server);

void free_clients(struct clients* clients);

void free_servers(struct servers* servers);

int add_fd_to_clients(int fd, size_t poll_index, struct clients* clients);

int add_fd_to_servers(int fd, struct sockaddr_in serv_addr, struct request* request,
                       struct servers* servers);

ssize_t get_header_value(char** value, size_t* value_len, char* header_name,
                         struct phr_header* headers, size_t num_headers);

#endif //INC_31_HTTP_SOCKET_H
