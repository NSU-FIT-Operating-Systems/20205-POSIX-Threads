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
#define WRITE_BUFFER_SIZE (64 * 1024)
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
int stop = 0;
size_t threads_total = 0;
size_t completed_threads = 0;
pthread_mutex_t mutex;

extern int errno;

void cleanup(void) {
    pthread_mutex_lock(&mutex);
    if (threads_total == completed_threads && stop == 1) {
        free_cache(cache);
        free_servers(&servers);
    }
    pthread_mutex_unlock(&mutex);
}

void mark_completed() {
    pthread_mutex_lock(&mutex);
    completed_threads++;
    pthread_mutex_unlock(&mutex);
}

void exit_routine(void) {
    mark_completed();
    cleanup();
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
    log_debug("\tSend %ztd", return_value);
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
    pthread_rwlock_wrlock(&server->response->rwlock);
    server->response->prev_buf_len = server->response->buf_len;
    server->response->buf_len += return_value;
    if (server->response->buf_len == server->response->buf_size) {
        server->response->buf_size *= 2;
        server->response->buf = (char*) realloc(server->response->buf, sizeof(char) * server->response->buf_size);
        if (server->response->buf == NULL) {
            log_error("realloc failed: %s", strerror(errno));
            pthread_rwlock_unlock(&server->response->rwlock);
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
                pthread_rwlock_unlock(&server->response->rwlock);
                close_server_and_subscribers(server);
                return -1;
            }
            if (return_value == 0) {
                char* content_length_str = NULL;
                return_value = buffer_to_string(content_length, content_length_len, &content_length_str);
                free(content_length);
                if (return_value != 0) {
                    pthread_rwlock_unlock(&server->response->rwlock);
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
    return_value = write(server->response->new_data_fd, &server->response->subscribers_count,
                         sizeof(server->response->subscribers_count));
    if (return_value < 0) {
        log_error("write to event new data: %s", strerror(errno));
        close_server_and_subscribers(server);
        pthread_rwlock_unlock(&server->response->rwlock);
        return -1;
    }
    pthread_rwlock_unlock(&server->response->rwlock);
    if (server->response->buf_len == server->response->content_length + server->response->not_content_length) {
        close_server(server);
        return 1;
    }
    return 0;
}

void* server_function(struct server* server) {
    connect_to_server(server);
    int ret_val;
    while ((ret_val = send_to_server(server)) == 0 && !stop);
    if (stop) {
        exit_routine();
        return (void*) EXIT_SUCCESS;
    }
    if (ret_val == -1) {
        mark_completed();
        return (void*) EXIT_FAILURE;
    }
    while ((ret_val = receive_from_server(server)) == 0 && !stop);
    if (stop) {
        exit_routine();
        return (void*) EXIT_SUCCESS;
    }
    if (ret_val == -1) {
        mark_completed();
        return (void*) EXIT_FAILURE;
    }
    mark_completed();
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
        subscribe(client, &cache, cache_node - cache.nodes);
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
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_t server;
    int err = pthread_create(&server, &attr, (void* (*)(void*)) server_function,
                             (void*) (servers.members + server_index));
    if (err != 0) {
        log_error("pthread_create failed: %s", strerror(err));
        return -1;
    }
    pthread_mutex_lock(&mutex);
    threads_total++;
    pthread_mutex_unlock(&mutex);
    pthread_attr_destroy(&attr);
    return RESPONSE_NOT_IN_CACHE;
}

int send_to_client(struct client* client) {
    pthread_rwlock_rdlock(&client->response->rwlock);
    ssize_t return_value;
//    log_debug("\trdlock");
    log_debug("\t1 buf_len = %d, bytes_written = %d", client->response->buf_len, client->bytes_written);
    if (client->response->buf_len - client->bytes_written == 0) {
        log_debug("\tevent fd %d", client->response->new_data_fd);
        unsigned long info;
        pthread_rwlock_unlock(&client->response->rwlock);
        return_value = read(client->response->new_data_fd, &info, sizeof(info));
        if (return_value < 0) {
            log_error("read event new data: %s", strerror(errno));
            close_client(client);
            return -1;
        }
        pthread_rwlock_rdlock(&client->response->rwlock);
    }
    size_t to_write = client->response->buf_len - client->bytes_written;
    log_debug("\t2 buf_len = %d, bytes_written = %d", client->response->buf_len, client->bytes_written);
    if (to_write > WRITE_BUFFER_SIZE) {
        to_write = WRITE_BUFFER_SIZE;
    }
    if (to_write == 0) {
        pthread_rwlock_unlock(&client->response->rwlock);
        return 0;
    }
    return_value = write(client->fd,
                         client->response->buf + client->bytes_written,
                         to_write);
    if (return_value < 0) {
        log_error("write to client: %s", strerror(errno));
        pthread_rwlock_unlock(&client->response->rwlock);
        close_client(client);
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
        pthread_rwlock_unlock(&client->response->rwlock);
        close_client(client);
        return 1;
    }
    pthread_rwlock_unlock(&client->response->rwlock);
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
    while ((ret_val = receive_from_client(&client)) == RECEIVING && !stop);
    if (stop) {
        exit_routine();
        return (void*) EXIT_SUCCESS;
    }
    if (ret_val == CLOSE) {
        mark_completed();
        return (void*) EXIT_FAILURE;
    }
    ret_val = subscribe_client(&client);
    if (ret_val == CLOSE) {
        mark_completed();
        return (void*) EXIT_FAILURE;
    }
    while ((ret_val = send_to_client(&client)) == 0 && !stop);
    if (stop) {
        exit_routine();
        return (void*) EXIT_SUCCESS;
    }
    if (ret_val == -1) {
        mark_completed();
        return (void*) EXIT_FAILURE;
    }
    mark_completed();
    return (void*) EXIT_SUCCESS;
}

int accept_client(int proxy_fd) {
    ssize_t client_fd = accept(proxy_fd, NULL, NULL);
    if (client_fd < 0) {
        log_error("accept failed: %s", strerror(errno));
        return -1;
    }
    log_debug("Accept client fd %d", client_fd);
    sigset_t mask, old_mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    int ret_val;
    if ((ret_val = pthread_sigmask(SIG_BLOCK, &mask, &old_mask)) != 0) {
        log_error("sigprocmask: %s", strerror(ret_val));
        return -1;
    }
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_t client;
    ret_val = pthread_create(&client, &attr, (void* (*)(void*)) client_function, (void*) client_fd);
    if (ret_val != 0) {
        log_error("pthread_create failed: %s", strerror(ret_val));
        return -1;
    }
    pthread_attr_destroy(&attr);
    pthread_mutex_lock(&mutex);
    threads_total++;
    pthread_mutex_unlock(&mutex);
    if ((ret_val = pthread_sigmask(SIG_SETMASK, &old_mask, NULL)) != 0) {
        log_error("sigprocmask: %s", strerror(ret_val));
        return -1;
    }
    return 0;
}

void sigmask_init(void) {
    struct sigaction new_actn, old_actn;
    new_actn.sa_handler = SIG_IGN;
    sigemptyset(&new_actn.sa_mask);
    new_actn.sa_flags = 0;
    sigaction(SIGPIPE, &new_actn, &old_actn);
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

void sig_handler(int signum) {
    stop = 1;
    cleanup();
    pthread_exit((void*) EXIT_SUCCESS);
}

int main(int argc, char* argv[]) {
    log_set_level(LOG_ERROR);
    int ret_val = 0;
    if (parse_args(argc, argv) != 0) {
        printf(USAGE);
        pthread_exit((void*) EXIT_FAILURE);
    }
    printf("IP address: %s\nPort: %ld\n", proxy_ip, proxy_port);
    int poll_fds_num = INIT_FDS_SIZE;
    int servers_size = poll_fds_num;
    ret_val = init_servers(&servers, servers_size);
    if (ret_val != 0) {
        log_error("init_servers: %s", strerror(errno));
        goto EXIT;
    }
    if (init_cache(&cache, CACHE_SIZE) != 0) {
        goto CLEANUP;
    }
    int proxy_fd;
    if ((proxy_fd = proxy_fd_init()) < 0) {
        goto CLEANUP;
    }
    signal(SIGINT, sig_handler);
    sigmask_init();
    pthread_mutex_init(&mutex, NULL);
    int iteration = 0;
    while (1) {
        log_debug("%d.", iteration++);
        if (accept_client(proxy_fd) != 0) {
            log_error("accept_client failed");
        }
    }
    CLEANUP:
    cleanup();
    EXIT:
    pthread_exit((void*) EXIT_FAILURE);
}
