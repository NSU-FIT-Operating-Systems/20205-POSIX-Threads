#define GNU_SOURCE

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <limits.h>
#include <sys/signalfd.h>
#include <signal.h>
#include <assert.h>
#include <pthread.h>
#include "socket_operations/socket_operations.h"
#include "http_socket/http_socket.h"
#include "http_socket/cache.h"
#include "log/src/log.h"

#define SA struct sockaddr
#define SERVER_PORT 80
#define PROXY_PORT 8080
#define PROXY_IP "0.0.0.0"
#define INIT_FDS_SIZE 200
#define USAGE "USAGE:\tproxy [IP PORT]\nWHERE:\tIP - IP address of proxy\n\tPORT - port of proxy\n"
#define READ_BUFFER_SIZE (32 * 1024)
#define WRITE_BUFFER_SIZE (32 * 1024)
#define CACHE_SIZE 10
#define CLOSE (-1)
#define RECEIVING 0
#define RECEIVED 1
#define RESPONSE_IN_CACHE 2
#define RESPONSE_NOT_IN_CACHE 1

long proxy_port = PROXY_PORT;
char* proxy_ip = PROXY_IP;
struct cache cache;
struct servers servers;

extern int errno;

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

void close_client(struct client* client) {
    if (client->fd >= 0) {
        close(client->fd);
    }
    unsubscribe(client);
    free_client(client);
}

void close_server(struct server* server) {
    close(server->fd);
    setup_server(server);
    servers.current_number--;
}

void close_server_and_subscribers(struct server* server) {
    for (int k = 0; k < server->response->subscribers_max_size; k++) {
        if (server->response->subscribers[k] != NULL) {
            close_client(server->response->subscribers[k]);
        }
    }
    close_server(server);
}

int connect_to_server(struct server* server) {
    if (connect(server->fd,
                (SA*) &server->serv_addr,
                sizeof(server->serv_addr)) != 0) {
        close_server_and_subscribers(server);
        return 1;
    } else {
        server->processed = 1;
        log_debug("\tConnect server fd %d", server->fd);
    }
    return 0;
}

int send_to_server(struct server* server) {
    ssize_t return_value;
    while ((return_value = write(server->fd, server->request->buf + server->bytes_written,
                                 server->request->buf_len - server->bytes_written)) == -1 &&
           errno == EINTR);
    if (return_value < 0) {
        log_error("write to server: %s", strerror(errno));
        close_server_and_subscribers(server);
        return -1;
    }
    log_debug("\tSend %zd", return_value);
    server->bytes_written += return_value;
    if (server->bytes_written == server->request->buf_len) {
        return 1;
    }
    return 0;
}

int receive_from_server(struct server* server) {
    log_debug("\tfd %d", server->fd);
    ssize_t return_value;
    size_t to_read = server->response->buf_size - server->response->buf_len;
    if (to_read > READ_BUFFER_SIZE) {
        to_read = READ_BUFFER_SIZE;
    }
    while ((return_value = read(server->fd, server->response->buf + server->response->buf_len,
                                to_read)) == -1 &&
           errno == EINTR);
    log_debug("\tReceive %zd bytes", return_value);
    if (return_value < 0) {
        log_error("read from server: %s", strerror(errno));
        close_server_and_subscribers(server);
        return -1;
    }
    if (return_value == 0) {
        close_server_and_subscribers(server);
        return 0;
    }
    pthread_mutex_lock(&server->response->mutex);
    server->response->prev_buf_len = server->response->buf_len;
    server->response->buf_len += return_value;
    if (server->response->buf_len == server->response->buf_size) {
        server->response->buf_size *= 2;
        server->response->buf = (char*) realloc(server->response->buf, sizeof(char) * server->response->buf_size);
        if (server->response->buf == NULL) {
            log_error("realloc failed: %s", strerror(errno));
            pthread_mutex_unlock(&server->response->mutex);
            close_server_and_subscribers(server);
            return -1;
        }
    }
    if (server->response->not_content_length == -1) {
        server->response->num_headers = server->response->headers_max_size;
        return_value = parse_response(server->response);
        log_debug("\tParse %zd", return_value);
        log_debug("\tResponse parsed");
        for (int i = 0; i < server->response->num_headers; i++) {
            log_debug("\t%.*s: %.*s", (int) server->response->headers[i].name_len, server->response->headers[i].name,
                      (int) server->response->headers[i].value_len, server->response->headers[i].value);
        }
        if (server->response->content_length == -1) {
            char* content_length;
            size_t content_length_len;
            return_value = get_header_value(&content_length, &content_length_len, "Content-Length",
                                            server->response->headers, server->response->num_headers);
            if (return_value == 2) {
                pthread_mutex_unlock(&server->response->mutex);
                close_server_and_subscribers(server);
                return -1;
            }
            if (return_value == 0) {
                char* content_length_str = NULL;
                return_value = buffer_to_string(content_length, content_length_len, &content_length_str);
                free(content_length);
                if (return_value != 0) {
                    pthread_mutex_unlock(&server->response->mutex);
                    close_server_and_subscribers(server);
                    return -1;
                }
                server->response->content_length = (int) strtol(content_length_str, NULL, 0);
                free(content_length_str);
            }
        }
        log_debug("\tContent length parsed");
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
    log_debug("\tClients num %d", server->response->subscribers_count);
    pthread_cond_broadcast(&server->response->cond);
    pthread_mutex_unlock(&server->response->mutex);
    if (server->response->buf_len == server->response->content_length + server->response->not_content_length) {
        close_server(server);
        return 1;
    }
    return 0;
}

void* server_function(struct server* server) {
    connect_to_server(server);
    int ret_val;
    while((ret_val = send_to_server(server)) == 0);
    if (ret_val == -1) {
        return (void*) EXIT_FAILURE;
    }
    while((ret_val = receive_from_server(server)) == 0);
    if (ret_val == -1) {
        return (void*) EXIT_FAILURE;
    }
    return (void*) EXIT_SUCCESS;
}

int receive_from_client(struct client* client) {
    ssize_t return_value;
    int parse_result;
    while ((return_value = read(client->fd,
                                client->request.buf + client->request.buf_len,
                                client->request.buf_size - client->request.buf_len)) == -1 &&
           errno == EINTR) {
        log_debug("Client recv");

    }

    log_debug("\tClient read");
    if (return_value < 0) {
        log_error("read from client: %s", strerror(errno));
        close_client(client);
        return CLOSE;
    } else {
        client->request.prev_buf_len = client->request.buf_len;
        client->request.buf_len += return_value;
        if (client->request.buf_len == 0) {
            close_client(client);
            return CLOSE;
        }
        log_debug("\tprev buf len: %zu, buf len: %zu", client->request.prev_buf_len, client->request.buf_len);
        client->request.num_headers = client->request.headers_max_size;
        parse_result = parse_request(&client->request);
        if (parse_result == -1) {
            log_error("request is too long");
            close_client(client);
            return CLOSE;
        }
        if (parse_result == 2) {
            log_debug("\tPart of request read");
            return RECEIVING;
        }
        if (client->request.buf_len == client->request.buf_size) {
            log_error("request is too long (%zu bytes)", client->request.buf_len);
            close_client(client);
            return CLOSE;
        }
        return RECEIVED;
    }
}

int subscribe_client(struct client* client) {
    if (strncmp("GET", client->request.method, client->request.method_len) != 0) {
        log_info("Method %.*s is not implemented",
                 (int) client->request.method_len, client->request.method);
        close_client(client);
        return CLOSE;
    }
    char* url = NULL;
    ssize_t return_value = buffer_to_string(client->request.path, client->request.path_len, &url);
    if (return_value != 0) {
        log_error("buffer_to_string failed");
        close_client(client);
        return CLOSE;
    }
    struct cache_node* cache_node = get(cache, url);
    if (cache_node != NULL && (cache_node->response->status == 200 || cache_node->response->status == -1)) {
        cache_node->response->subscribers[cache_node->response->subscribers_count++] = client;
        client->response = cache_node->response;
        log_debug("\tSubscribe to cache");
        free(url);
        return RESPONSE_IN_CACHE;
    }
    char* hostname;
    size_t hostname_len;
    return_value = get_header_value(&hostname, &hostname_len, "Host",
                                    client->request.headers, client->request.num_headers);
    if (return_value > 0) {
        log_error("get_header_value failed");
        free(url);
        return CLOSE;
    }
    char* ip = (char*) malloc(sizeof(char) * 100);
    if (ip == NULL) {
        log_error("malloc failed: %s", strerror(errno));
        close_client(client);
        free(url);
        return CLOSE;
    }
    return_value = hostname_to_ip(hostname, hostname_len, ip);
    free(hostname);
    assert(ip != NULL);
    if (return_value != 0) {
        log_error("hostname_to_ip failed");
        close_client(client);
        free(ip);
        free(url);
        return CLOSE;
    }
    struct sockaddr_in serv_addr;
    ssize_t server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        log_error("socket: %s", strerror(errno));
        close_client(client);
        free(ip);
        free(url);
        return CLOSE;
    }
    bzero(&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(ip);
    free(ip);
    serv_addr.sin_port = htons(SERVER_PORT);
    int server_index;
    server_index = add_fd_to_servers((int) server_fd, serv_addr,
                                     &client->request, &servers);
    if (server_index < 0) {
        log_error("too many connections");
        free(url);
        return CLOSE;
    }
    log_debug("\tServer index %d", server_index);
    size_t cache_index = set(&cache, url);
    subscribe(client, &cache, cache_index);
    make_publisher(servers.members + server_index, &cache, cache_index);
    pthread_t server;
    int err = pthread_create(&server, NULL, (void* (*)(void*)) server_function,
                             (void*) (servers.members + server_index));
    if (err != 0) {
        log_error("pthread_create failed: %s", strerror(err));
        return -1;
    }
    return RESPONSE_NOT_IN_CACHE;
}

int send_to_client(struct client* client) {
    pthread_mutex_lock(&client->response->mutex);
    ssize_t return_value;
    while (client->response->buf_len - client->bytes_written == 0) {
        pthread_cond_wait(&client->response->cond, &client->response->mutex);
    }
    size_t to_write = client->response->buf_len - client->bytes_written;
    log_debug("\tbuf_len = %d, bytes_written = %d", client->response->buf_len, client->bytes_written);
    if (to_write > WRITE_BUFFER_SIZE) {
        to_write = WRITE_BUFFER_SIZE;
    }
    return_value = write(client->fd,
                         client->response->buf + client->bytes_written,
                         to_write);
    if (return_value < 0) {
        log_error("write to client: %s", strerror(errno));
        close(client->fd);
        free_client(client);
        return -1;
    }
    log_debug("\tSend %zd bytes", return_value);
    client->bytes_written += return_value;
    if ((client->response->content_length + client->response->not_content_length == client->bytes_written &&
         client->response->content_length != -1 && client->response->not_content_length != -1) ||
        return_value == 0) {
        if (client->response->status != 200) {
            log_debug("\tcache node index: %d", client->cache_node);
            assert(client->cache_node >= 0 && client->cache_node < cache.size);
            clear_cache_node(&(cache.nodes[client->cache_node]));
        }
        pthread_mutex_unlock(&client->response->mutex);
        unsubscribe(client);
        close(client->fd);
        free_client(client);
        return 1;
    }
    pthread_mutex_unlock(&client->response->mutex);
    sched_yield();
    return 0;
}

void* client_function(ssize_t client_fd) {
    struct client client;
    if (init_client(&client) != 0) {
        log_error("init_client failed: %s", strerror(errno));
    }
    client.fd = (int) client_fd;
    log_debug("client fd = %d", client_fd);
    int ret_val;
    while ((ret_val = receive_from_client(&client)) == RECEIVING);
    if (ret_val == CLOSE) {
        return (void*) EXIT_FAILURE;
    }
    ret_val = subscribe_client(&client);
    if (ret_val == CLOSE) {
        return (void*) EXIT_FAILURE;
    }
    while ((ret_val = send_to_client(&client)) == 0);
    if (ret_val == -1) {
        return (void*) EXIT_FAILURE;
    }
    return (void*) EXIT_SUCCESS;
}

int accept_client(int proxy_fd) {
    ssize_t client_fd = accept(proxy_fd, NULL, NULL);
    if (client_fd < 0) {
        log_error("accept failed: %s", strerror(errno));
        return -1;
    }
    log_debug("Accept client fd %d", client_fd);
    pthread_t client;
    int err = pthread_create(&client, NULL, (void* (*)(void*)) client_function, (void*) client_fd);
    if (err != 0) {
        log_error("pthread_create failed: %s", strerror(err));
        return -1;
    }
    return 0;
}

int sigmask_init(void) {
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT | SIGPIPE);
    int ret_val;
    if ((ret_val = pthread_sigmask(SIG_BLOCK, &mask, NULL)) != 0) {
        log_error("sigprocmask: %s", strerror(ret_val));
        return -1;
    }
    return 0;
}

int proxy_fd_init(void) {
    int proxy_fd;
    struct sockaddr_in proxyaddr;
    proxy_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (proxy_fd == -1) {
        log_error("socket creation failed: %s", strerror(errno));
        return -1;
    }
    int optval = 1;
    setsockopt(proxy_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    bzero(&proxyaddr, sizeof(proxyaddr));
    proxyaddr.sin_family = AF_INET;
    proxyaddr.sin_addr.s_addr = inet_addr(proxy_ip);
    proxyaddr.sin_port = htons(proxy_port);
    if ((bind(proxy_fd, (SA*) &proxyaddr, sizeof(proxyaddr))) != 0) {
        log_error("socket bind failed: %s", strerror(errno));
        close(proxy_fd);
        return -1;
    }
    if ((listen(proxy_fd, SOMAXCONN)) != 0) {
        log_error("listen failed: %s", strerror(errno));
        close(proxy_fd);
        return -1;
    }
    return proxy_fd;
}

int parse_args(int argc, char* argv[]) {
    if (argc == 3) {
        proxy_ip = argv[1];
        proxy_port = strtol(argv[2], NULL, 0);
        if (proxy_port == LONG_MIN || proxy_port == LONG_MAX) {
            log_error("strtol failed");
            return -1;
        }
        return 0;
    } else if (argc == 1) {
        return 0;
    }
    return -1;
}

int main(int argc, char* argv[]) {
    log_set_level(LOG_ERROR);
    int error = 0;
    int ret_val = 0;
    if (parse_args(argc, argv) != 0) {
        printf(USAGE);
        return EXIT_FAILURE;
    }
    printf("IP address: %s\nPort: %ld\n", proxy_ip, proxy_port);
    int poll_fds_num = INIT_FDS_SIZE;
    int servers_size = poll_fds_num;
    ret_val = init_servers(&servers, servers_size);
    if (ret_val != 0) {
        log_error("init_servers: %s", strerror(errno));
        error = 1;
        goto EXIT;
    }
    if (init_cache(&cache, CACHE_SIZE) != 0) {
        error = 1;
        goto CLEANUP;
    }
    int proxy_fd;
    if ((proxy_fd = proxy_fd_init()) < 0) {
        error = 1;
        goto CLEANUP;
    }
    if (sigmask_init() != 0) {
        error = 1;
        goto CLEANUP;
    }
    int iteration = 0;
    while (1) {
        log_debug("%d.", iteration++);
        log_debug("====Accept client====");
        if (accept_client(proxy_fd) != 0) {
            log_error("accept_client failed");
        }
        log_debug("====================\n");
    }
    CLEANUP:
    free_cache(cache);
    free_servers(&servers);
    EXIT:
    if (error) {
        pthread_exit((void*) EXIT_FAILURE);
    }
    pthread_exit((void*) EXIT_SUCCESS);
}
