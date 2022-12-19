#define _GNU_SOURCE

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/poll.h>
#include <limits.h>
#include <sys/signalfd.h>
#include <signal.h>
#include "socket_operations/socket_operations.h"
#include "http_socket/http_socket.h"
#include "http_socket/cache.h"
#include "http_socket/picohttpparser/picohttpparser.h"

#define SA struct sockaddr
#define SERVER_PORT 80
#define PROXY_PORT 8080
#define PROXY_IP "0.0.0.0"
#define BUF_SIZE 4096
#define MAX_FDS 20
#define USAGE "proxy IP PORT"
#define WRITE_BUFFER_SIZE 30000

long proxy_port = PROXY_PORT;
char* proxy_ip = PROXY_IP;
struct cache cache;

extern int errno;

int add_fd_to_poll(int fd, short events, struct pollfd* poll_fds, size_t poll_fds_num) {
    for (int i = 0; i < poll_fds_num; i++) {
        if (poll_fds[i].fd == -1) {
            poll_fds[i].fd = fd;
            poll_fds[i].events = events;
            return i;
        }
    }
    return -1;
}

int remove_from_poll(int fd, struct pollfd* poll_fds, size_t poll_fds_num) {
    for (int i = 0; i < poll_fds_num; i++) {
        if (poll_fds[i].fd == fd) {
            poll_fds[i].fd = -1;
            return i;
        }
    }
    return -1;
}

void close_all(struct pollfd* poll_fds, size_t poll_fds_num) {
    for (int i = 0; i < poll_fds_num; i++) {
        if (poll_fds[i].fd != -1) {
            close(poll_fds[i].fd);
        }
    }
}

ssize_t get_substring(char* substring, char* string, size_t string_length) {
    char* p1, * p2, * p3;
    p1 = string;
    p2 = substring;
    int substring_beginning;
    for (int i = 0; i < string_length; i++) {
        if (*p1 == *p2) {
            p3 = p1;
            int j;
            for (j = 0; j < strlen(substring); j++) {
                if (*p3 == *p2) {
                    p3++;
                    p2++;
                } else {
                    break;
                }
            }
            p2 = substring;
            if (strlen(substring) == j) {
                substring_beginning = i;
                return substring_beginning;
            }
        }
        p1++;
    }
    return -1;
}

void close_client(int fd, struct pollfd* poll_fds, size_t poll_fds_num, struct client* clients, size_t client_index) {
    if (fd >= 0) {
        close(fd);
        remove_from_poll(fd, poll_fds, poll_fds_num);
        if (client_index < 0) {
            for (int i = 0; i < poll_fds_num; i++) {
                if (clients[i].fd == fd) {
                    setup_client(i, clients);
                    break;
                }
            }
        } else {
            setup_client(client_index, clients);
        }
    }
}

void close_server(int fd, struct pollfd* poll_fds, size_t poll_fds_num, struct server* servers, size_t server_index) {
    close(fd);
    remove_from_poll(fd, poll_fds, poll_fds_num);
    setup_server(server_index, servers);
}

int receive_from_client(struct client* client, size_t client_index, struct pollfd* poll_fds, size_t poll_fds_num,
                        struct client* clients,
                        struct server* servers, size_t servers_size) {
    ssize_t return_value;
    int parse_result;
    while ((return_value = read(client->fd,
                                client->request.buf + client->request.buf_len,
                                client->request.buf_size - client->request.buf_len)) == -1 &&
           errno == EINTR);
    printf("Client read\n");
    if (return_value < 0) {
        perror("read from client");
        close_client(client->fd, poll_fds, poll_fds_num, clients, client_index);
        return 1;
    } else { // TODO handle 0 correctly
        client->request.prev_buf_len = client->request.buf_len;
        client->request.buf_len += return_value;
        if (client->request.buf_len == 0) {
            close_client(client->fd, poll_fds, poll_fds_num, clients, client_index);
            return 0;
        }
        printf("prev buf len: %zu, buf len: %zu\n", client->request.prev_buf_len, client->request.buf_len);
        client->request.num_headers = client->request.headers_max_size;
        parse_result = phr_parse_request(client->request.buf,
                                         client->request.buf_len,
                                         (const char**) &client->request.method,
                                         &client->request.method_len,
                                         (const char**) &client->request.path,
                                         &client->request.path_len,
                                         &client->request.minor_version,
                                         client->request.headers,
                                         &client->request.num_headers,
                                         client->request.prev_buf_len);
        if (parse_result == -1) {
            fprintf(stderr, "request is too long\n");
            close_client(client->fd, poll_fds, poll_fds_num, clients, client_index);
            return 1;
        }
        if (parse_result == 2) {
            printf("Part of request read\n");
            return 0;
        }
        if (client->request.buf_len == client->request.buf_size) {
            fprintf(stderr, "request is too long (%zu Kb)\n", client->request.buf_len);
            close_client(client->fd, poll_fds, poll_fds_num, clients, client_index);
            return 1;
        }
        if (strncmp("GET", client->request.method, client->request.method_len) != 0) {
            fprintf(stderr, "Method %.*s is not implemented\n",
                    (int) client->request.method_len, client->request.method);
            close_client(client->fd, poll_fds, poll_fds_num, clients, client_index);
            return 1;
        }
        char* url = NULL;
        return_value = buffer_to_string(clients->request.path, clients->request.path_len, &url);
        if (return_value != 0) {
            fprintf(stderr, "buffer_to_string failed\n");
            close_client(client->fd, poll_fds, poll_fds_num, clients, client_index);
            return 1;
        }
        struct cached_response* cached_response = get_cached_response(cache, url);
        if (cached_response != NULL) {
            cached_response->response->clients[cached_response->response->clients_num++] = client;
            client->response = cached_response->response;
            if (cached_response->response->buf_len > 0) {
                poll_fds[client->poll_index].events = POLLOUT;
            } else {
                poll_fds[client->poll_index].events = 0;
            }
            printf("Subscribe to cache\n");
            free(url);
            return 0;
        }
        char* hostname;
        size_t hostname_len;
        return_value = get_header_value(&hostname, &hostname_len, "Host",
                                        client->request.headers, client->request.num_headers);
        if (return_value > 0) {
            fprintf(stderr, "get_header_value failed\n");
            free(url);
            return 1;
        }
        char* ip = (char*) malloc(sizeof(char) * 100);
        if (ip == NULL) {
            perror("malloc failed");
            close_client(client->fd, poll_fds, poll_fds_num, clients, client_index);
            free(url);
            return 1;
        }
        return_value = hostname_to_ip(hostname, hostname_len, ip);
        free(hostname);
        if (return_value != 0) {
            fprintf(stderr, "hostname_to_ip failed\n");
            close_client(client->fd, poll_fds, poll_fds_num, clients, client_index);
            free(ip);
            free(url);
            return 1;
        }
        int server_fd;
        struct sockaddr_in serv_addr;
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd == -1) {
            perror("socket");
            close_client(client->fd, poll_fds, poll_fds_num, clients, client_index);
            free(ip);
            free(url);
            return 1;
        }
        bzero(&serv_addr, sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_addr.s_addr = inet_addr(ip);
        free(ip);
        serv_addr.sin_port = htons(SERVER_PORT);
        int poll_index = add_fd_to_poll(server_fd, POLLOUT, poll_fds, poll_fds_num);
        int server_index;
        if (poll_index >= 0) {
            server_index = add_fd_to_servers(server_fd, serv_addr, poll_index,
                                             &client->request, servers, servers_size);
        }
        if (poll_index < 0 || server_index < 0) {
            fprintf(stderr, "too many connections\n");
            free(url);
            return 1;
        }
        printf("Server index %d\n", server_index);
        servers[server_index].response->clients[servers[server_index].response->clients_num++] = client;
        add_response_to_cache(cache, url, servers[server_index].response);
        client->response = servers[server_index].response;
        poll_fds[client->poll_index].events = 0;
        printf("Client poll_index %zu\n", servers[server_index].response->clients[0]->poll_index);
    }
    return 0;
}

int send_to_client(struct client* client, size_t client_index, struct pollfd* poll_fds, size_t poll_fds_num,
                   struct client* clients) {
    ssize_t return_value = 0;
    size_t to_write = client->response->buf_len - client->bytes_written;
    if (to_write > WRITE_BUFFER_SIZE) {
        to_write = WRITE_BUFFER_SIZE;
    }
    while ((return_value = write(client->fd, client->response->buf + client->bytes_written,
                                 to_write)) == -1 &&
           errno == EINTR);
    if (return_value < 0) {
        perror("write to client");
        close(client->fd);
        remove_from_poll(client->fd, poll_fds, poll_fds_num);
        setup_client(client_index, clients);
        return 1;
    }
    client->bytes_written += return_value;
    if (client->response->buf_len == client->bytes_written) {
        poll_fds[client->poll_index].events = 0;
    }
    if (return_value == 0) {
        close(client->fd);
        remove_from_poll(client->fd, poll_fds, poll_fds_num);
        setup_client(client_index, clients);
        return 0;
    }
    if (client->response->content_length + client->response->not_content_length == client->bytes_written &&
        client->response->content_length != 1 &&
        client->response->not_content_length != 1) {
        close(client->fd);
        remove_from_poll(client->fd, poll_fds, poll_fds_num);
        setup_client(client_index, clients);
        return 0;
    }
    return 0;
}

int process_clients(struct pollfd* poll_fds, size_t poll_fds_num, struct client* clients, size_t clients_size,
                    struct server* servers, size_t servers_size) {
    for (size_t i = 0; i < clients_size; i++) {
//        printf("i = %zu, size = %zu\n", i, clients_size);
        struct client* client = &clients[i];
        if (client->fd != -1) {
            if ((poll_fds[client->poll_index].revents & POLLHUP) == POLLHUP) {
                close(client->fd);
                remove_from_poll(client->fd, poll_fds, poll_fds_num);
                setup_client(i, clients);
                continue;
            }
            if (!client->processed) {
                if ((poll_fds[client->poll_index].revents & POLLIN) == POLLIN) {
                    printf("Receive from client %zu. fd: %d, processed: %d, revents: %d\n", i, client->fd,
                           client->processed,
                           poll_fds[client->poll_index].revents);
                    receive_from_client(client, i, poll_fds, poll_fds_num, clients, servers, servers_size);
                } else if ((poll_fds[client->poll_index].revents & POLLOUT) == POLLOUT) {
                    printf("Send to client %zu. fd: %d, processed: %d, revents: %d\n", i, client->fd, client->processed,
                           poll_fds[client->poll_index].revents);
                    send_to_client(client, i, poll_fds, poll_fds_num, clients);
                }
            }
        }
    }
    return 0;
}

int connect_to_server(struct server* server, int server_index, struct pollfd* poll_fds, size_t poll_fds_num,
                      struct server* servers) {
    if (connect(server->fd,
                (SA*) &server->serv_addr,
                sizeof(server->serv_addr)) != 0) {
        remove_from_poll(server->fd, poll_fds, poll_fds_num);
        close(server->fd);
        setup_server(server_index, servers);
    } else {
        server->processed = 1;
    }
    return 0;
}

int send_to_server(struct server* server, int server_index, struct pollfd* poll_fds, size_t poll_fds_num,
                   struct server* servers) {
    ssize_t return_value;
    while ((return_value = write(server->fd, server->request->buf + server->bytes_written,
                                 server->request->buf_len - server->bytes_written)) == -1 &&
           errno == EINTR);
    if (return_value < 0) {
        perror("write to server");
        close_server(server->fd, poll_fds, poll_fds_num, servers, server_index);
        return 1;
    } else if (return_value == 0) {
        poll_fds[server->poll_index].events = POLLIN;
    }
    server->bytes_written += return_value;
    return 0;
}

int receive_from_server(struct server* server, int server_index, struct pollfd* poll_fds, size_t poll_fds_num,
                        struct client* clients,
                        struct server* servers) {
    printf("Server index %d\n", server_index);
    ssize_t return_value;
    while ((return_value = read(server->fd, server->response->buf + server->response->buf_len,
                                server->response->buf_size - server->response->buf_len)) == -1 &&
           errno == EINTR);
    printf("Read from server %zd\n", return_value);
    if (return_value < 0) {
        perror("read from server");
        for (int k = 0; k < server->response->clients_num; k++) {
            close_client(server->response->clients[k]->fd, poll_fds, poll_fds_num, clients, -1);
        }
        close_server(server->fd, poll_fds, poll_fds_num, servers, server_index);
        return 1;
    }
    if (return_value == 0) {
        for (int k = 0; k < server->response->clients_num; k++) {
            poll_fds[server->response->clients[k]->poll_index].events = POLLOUT;
        }
        close_server(server->fd, poll_fds, poll_fds_num, servers, server_index);
        return 0;
    }
    server->response->prev_buf_len = server->response->buf_len;
    server->response->buf_len += return_value;
    if (server->response->buf_len == server->response->buf_size) {
        server->response->buf_size *= 2;
        server->response->buf = (char*) realloc(server->response->buf, sizeof(char) * server->response->buf_size);
        if (server->response->buf == NULL) {
            perror("realloc failed");
            for (int k = 0; k < server->response->clients_num; k++) {
                close_client(server->response->clients[k]->fd, poll_fds, poll_fds_num, clients, -1);
            }
            close_server(server->fd, poll_fds, poll_fds_num, servers, server_index);
            return 1;
        }
    }
    if (server->response->not_content_length == -1) {
        server->response->num_headers = server->response->headers_max_size;
        return_value = phr_parse_response(server->response->buf,
                                          server->response->buf_len,
                                          &server->response->minor_version,
                                          &server->response->status,
                                          (const char**) &server->response->message,
                                          &server->response->message_len,
                                          server->response->headers,
                                          &server->response->num_headers,
                                          server->response->prev_buf_len
        );
        printf("Parse %zd\n", return_value);
        printf("Response parsed\n");
        for (int i = 0; i < server->response->num_headers; i++) {
            printf("%.*s: %.*s\n", (int) server->response->headers[i].name_len, server->response->headers[i].name,
                   (int) server->response->headers[i].value_len, server->response->headers[i].value);
        }
        if (server->response->content_length == -1) {
            char* content_length;
            size_t content_length_len;
            return_value = get_header_value(&content_length, &content_length_len, "Content-Length",
                                            server->response->headers, server->response->num_headers);
            if (return_value == 2) {
                for (int k = 0; k < server->response->clients_num; k++) {
                    close_client(server->response->clients[k]->fd, poll_fds, poll_fds_num, clients, -1);
                }
                close_server(server->fd, poll_fds, poll_fds_num, servers, server_index);
                return 1;
            }
            if (return_value == 0) {
                char* content_length_str = NULL;
                return_value = buffer_to_string(content_length, content_length_len, &content_length_str);
                free(content_length);
                if (return_value != 0) {
                    return -1;
                }
                server->response->content_length = (int) strtol(content_length_str, NULL, 0);
                free(content_length_str);
            }
        }
        printf("Content length parsed\n");
        if (server->response->not_content_length == -1) {
            return_value = get_substring("\r\n\r\n", server->response->buf, server->response->buf_len);
            if (return_value >= 0) {
                server->response->not_content_length = (int) return_value + 4;
                if (server->response->content_length == -1) {
                    server->response->content_length = 0;
                }
            }
        }
    }
    printf("Clients num %d\n", server->response->clients_num);
    printf("Client 0 poll_index %zu\n", server->response->clients[0]->poll_index);
    printf("Not content length parsed\n");
    for (int k = 0; k < server->response->clients_num; k++) {
        printf("Client poll_index %zu", server->response->clients[k]->poll_index);
        poll_fds[server->response->clients[k]->poll_index].events = POLLOUT;
    }
    printf("Clients POLLOUT\n");
    return 0;
}

int process_servers(struct pollfd* poll_fds, size_t poll_fds_num, struct client* clients,
                    struct server* servers, size_t servers_size) {
    for (int i = 0; i < servers_size; i++) {
        struct server* server = &servers[i];
        if (server->fd != -1 && !server->processed) {
            if ((poll_fds[server->poll_index].revents & (POLLOUT | POLLHUP)) == (POLLOUT | POLLHUP)) {
                connect_to_server(server, i, poll_fds, poll_fds_num, servers);
            } else if ((poll_fds[server->poll_index].revents & POLLOUT) == POLLOUT) {
                send_to_server(server, i, poll_fds, poll_fds_num, servers);
            } else if ((poll_fds[server->poll_index].revents & POLLIN) == POLLIN) {
                receive_from_server(server, i, poll_fds, poll_fds_num, clients, servers);
            }
        }
    }
    return 0;
}

int accept_client(struct pollfd* poll_fds, size_t poll_fds_num,
                  struct client* clients, size_t clients_size) {
    if (poll_fds[0].revents & POLLIN) {
        int client_fd = accept(poll_fds[0].fd, NULL, NULL);
        if (client_fd < 0) {
            perror("accept failed");
            return -1;
        } else {
        }
        printf("Accept client %d\n", client_fd);
        int poll_index = add_fd_to_poll(client_fd, POLLIN, poll_fds, poll_fds_num);
        int client_index;
        if (poll_index >= 0) {
            client_index = add_fd_to_clients(client_fd, poll_index, clients, clients_size);
        }
        if (poll_index < 0 || client_index < 0) {
            fprintf(stderr, "too many connections\n");
            return -1;
        }
    }
    return 0;
}

int process_signal(struct pollfd poll_socket_fd) {
    if ((poll_socket_fd.revents & POLLIN) == POLLIN) {
        struct signalfd_siginfo signal_fd_info;
        ssize_t return_value = read(poll_socket_fd.fd, &signal_fd_info, sizeof(signal_fd_info));
        if (return_value != sizeof(signal_fd_info)) {
            perror("read");
            return 2;
        }
        if (signal_fd_info.ssi_signo == SIGINT) {
            return 1;
        } else {
            return 2;
        }
    }
    return 0;
}

int signal_fd_init(struct pollfd* poll_fds) {
    sigset_t mask;
    int signal_fd;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1) {
        perror("sigprocmask");
    }
    signal_fd = signalfd(-1, &mask, 0);
    if (signal_fd == -1) {
        perror("signalfd");
        return 1;
    }
    poll_fds[1].fd = signal_fd;
    poll_fds[1].events = POLLIN;
    return 0;
}

int proxy_fd_init(struct pollfd* poll_fds) {
    int proxy_fd;
    struct sockaddr_in proxyaddr;
    proxy_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (proxy_fd == -1) {
        perror("socket creation failed");
        return -1;
    } else {
    }
    bzero(&proxyaddr, sizeof(proxyaddr));
    proxyaddr.sin_family = AF_INET;
    proxyaddr.sin_addr.s_addr = inet_addr(PROXY_IP);
    proxyaddr.sin_port = htons(PROXY_PORT);
    if ((bind(proxy_fd, (SA*) &proxyaddr, sizeof(proxyaddr))) != 0) {
        perror("socket bind failed");
        close(proxy_fd);
        return -1;
    } else {
    }
    if ((listen(proxy_fd, SOMAXCONN)) != 0) {
        perror("listen failed");
        close(proxy_fd);
        return -1;
    } else {
    }
    poll_fds[0].fd = proxy_fd;
    poll_fds[0].events = POLLIN;
    return 0;
}

int parse_args(int argc, char* argv[]) {
    if (argc == 3) {
        proxy_ip = argv[1];
        proxy_port = strtol(argv[2], NULL, 0);
        if (proxy_port == LONG_MIN || proxy_port == LONG_MAX) {
            perror("strtol failed");
            return -1;
        }
    } else if (argc == 1) {
        return 0;
    }
    return -1;
}

void init_poll_fds(struct pollfd* poll_fds, size_t size) {
    for (int i = 0; i < size; i++) {
        poll_fds[i].fd = -1;
    }
}

void init_sig_mask(sigset_t* sig_mask) {
    sigemptyset(sig_mask);
    sigaddset(sig_mask, SIGINT);
}

//TODO handle POLLERR POLLHUP. Segmentation fault when a lot of clients

int main(int argc, char* argv[]) {
    int error = 0;
    if (parse_args(argc, argv) != 0) {
        return EXIT_FAILURE;
    }
    int poll_fds_num = MAX_FDS;
    int clients_size = poll_fds_num;
    int servers_size = poll_fds_num;
    struct pollfd poll_fds[poll_fds_num];
    struct client clients[clients_size];
    struct server servers[servers_size];
    if (init_clients(clients, clients_size) != 0) {
        error = 1;
        goto CLEANUP;
    }
    if (init_servers(servers, servers_size) != 0) {
        error = 1;
        goto CLEANUP;
    }
    init_poll_fds(poll_fds, poll_fds_num);
    if (proxy_fd_init(poll_fds) != 0 || signal_fd_init(poll_fds) != 0) {
        error = 1;
        goto CLEANUP;
    }
    init_cache(&cache);
    sigset_t sig_mask;
    init_sig_mask(&sig_mask);
    int iteration = 0;
    while (1) {
        printf("%d.\n", iteration++);
        int poll_return = ppoll(poll_fds, poll_fds_num, NULL, &sig_mask);
        if (poll_return == -1) {
            if (errno != EINTR) {
                perror("poll failed");
                error = 1;
                goto CLEANUP;
            }
        }
        printf("Return from poll\n");
//        for (int i = 0; i < poll_fds_num; i++) {
//            printf("%d ", poll_fds[i].revents);
//        }

        printf("====Process signal====\n");
        int return_value = process_signal(poll_fds[1]);
        if (return_value != 0) {
            if (return_value == 2) {
                error = 1;
            }
            goto CLEANUP;
        }
        printf("=====================\n\n====Accept client====\n");
        if (accept_client(poll_fds, poll_fds_num, clients, clients_size) != 0) {
            fprintf(stderr, "accept_client failed\n\n");
        }
        printf("=====================\n\n===Process clients===\n");
        process_clients(poll_fds, poll_fds_num, clients, clients_size, servers, servers_size);
        printf("=====================\n\n===Process servers===\n");
        process_servers(poll_fds, poll_fds_num, clients, servers, servers_size);
        printf("=====================\n\n");
        for (int i = 0; i < poll_fds_num; i++) {
            servers[i].processed = 0;
            clients[i].processed = 0;
        }
    }
    CLEANUP:
    free_clients(clients, clients_size);
    free_servers(servers, servers_size);
    close_all(poll_fds, poll_fds_num);
    free_cache(cache);
    if (error) {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}