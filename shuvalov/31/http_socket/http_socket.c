#include <malloc.h>
#include <string.h>
#include "http_socket.h"

#define BUF_SIZE 4096

int parse_request(struct request* request) {
    return phr_parse_request(request->buf,
                             request->buf_len,
                             (const char**) &request->method,
                             &request->method_len,
                             (const char**) &request->path,
                             &request->path_len,
                             &request->minor_version,
                             request->headers,
                             &request->num_headers,
                             request->prev_buf_len);
}

int parse_response(struct response* response) {
    return phr_parse_response(response->buf,
                              response->buf_len,
                              &response->minor_version,
                              &response->status,
                              (const char**) &response->message,
                              &response->message_len,
                              response->headers,
                              &response->num_headers,
                              response->prev_buf_len);
}

int init_clients_members(struct client* clients, size_t size) {
    for (int i = 0; i < size; i++) {
        if (init_client(clients + i) != 0) {
            return -1;
        }
    }
    return 0;
}

int init_clients(struct clients* clients, size_t size) {
    clients->size = size;
    clients->current_number = 0;
    clients->members = (struct client*) malloc(sizeof(struct client) * size);
    if (clients->members == NULL) {
        return -1;
    }
    return init_clients_members(clients->members, size);
}

int init_servers(struct servers* servers, size_t size) {
    servers->size = size;
    servers->current_number = 0;
    servers->members = (struct server*) malloc(sizeof(struct server) * size);
    if (servers->members == NULL) {
        return -1;
    }
    init_servers_members(servers->members, size);
    return 0;
}

int init_client(struct client* client) {
    setup_client(client);
    client->request.buf = (char*) malloc(client->request.buf_size * sizeof(char));
    if (client->request.buf == NULL) {
        return -1;
    }
    client->request.headers = (struct phr_header*) malloc(
            client->request.headers_max_size * sizeof(*client->request.headers));
    if (client->request.headers == NULL) {
        return -1;
    }
    return 0;
}

void setup_client(struct client* client) {
    client->fd = -1;
    client->cache_node = -1;
    client->processed = 0;
    client->request.buf_size = BUF_SIZE;
    client->request.headers_max_size = 100;
    client->request.buf_len = 0;
    client->request.prev_buf_len = 0;
    client->bytes_written = 0;
    client->response = NULL;
}

void init_servers_members(struct server* servers, size_t size) {
    for (int i = 0; i < size; i++) {
        setup_server(i, servers);
    }
}

void setup_server(size_t index, struct server* servers) {
    servers[index].fd = -1;
    servers[index].processed = 0;
    servers[index].bytes_written = 0;
    servers[index].request = NULL;
    servers[index].response = NULL;
}

void free_clients(struct clients* clients) {
    for (int i = 0; i < clients->size; i++) {
        if (clients->members[i].request.buf != NULL) {
            free(clients->members[i].request.buf);
            free(clients->members[i].request.headers);
        }
    }
    free(clients->members);
}

void free_servers(struct servers* servers) {
    free(servers->members);
}

int add_fd_to_clients(int fd, size_t poll_index, struct clients* clients) {
    if (clients->current_number == clients->size) {
        printf("here\n");
        clients->members = realloc(clients->members, clients->size * 2 * sizeof(struct client));
        if (clients->members == NULL) {
            return -1;
        }
        if (init_clients_members(clients->members + clients->size, clients->size) != 0) {
            return -1;
        }
        clients->size *= 2;
    }
    for (int i = 0; i < clients->size; i++) {
        if (clients->members[i].fd == -1) {
            clients->members[i].fd = fd;
            clients->members[i].processed = 1;
            clients->members[i].poll_index = poll_index;
            clients->current_number++;
            return i;
        }
    }
    return -1;
}

int add_fd_to_servers(int fd, struct sockaddr_in serv_addr,
                      size_t poll_index, struct request* request,
                      struct servers* servers) {
    if (servers->current_number == servers->size) {
        servers->members = realloc(servers->members, servers->size * 2 * sizeof(struct server));
        if (servers->members == NULL) {
            return -1;
        }
        init_servers_members(servers->members + servers->size, servers->size);
        servers->size *= 2;
    }
    for (int i = 0; i < servers->size; i++) {
        if (servers->members[i].fd == -1) {
            servers->members[i].fd = fd;
            servers->members[i].processed = 1;
            servers->members[i].serv_addr = serv_addr;
            servers->members[i].poll_index = poll_index;
            servers->members[i].request = request;
            servers->current_number++;
            return i;
        }
    }
    return -1;
}

ssize_t get_header_value(char** value, size_t* value_len, char* header_name,
                         struct phr_header* headers, size_t num_headers) {
    for (size_t i = 0; i < num_headers; i++) {
        if (strncmp(headers[i].name, header_name, headers[i].name_len) == 0) {
            *value_len = headers[i].value_len;
            *value = (char*) malloc(sizeof(char) * *value_len);
            if (*value == NULL) {
                return 2;
            }
            strncpy(*value, headers[i].value, *value_len);
            return 0;
        }
    }
    *value = NULL;
    return 1;
}
