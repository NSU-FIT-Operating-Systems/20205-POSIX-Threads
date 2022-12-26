#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <poll.h>
#include <fcntl.h>
#include <errno.h>
#include "cache.h"

#define POLL_TIMEOUT 1000
#define CLEANUP_TIMEOUT 10
#define URL_BUFF_SIZE 1024
#define GET_REQ_SIZE 8
#define PORT 80
#define HTTP_DELIM "://"
#define GET_PREFIX "GET "

// Errors
#define UNABLE_TO_FIND_HOST (-1)
#define UNABLE_TO_CREATE_SOCKET (-2)
#define UNABLE_TO_CONNECT (-3)

#define EXP_GROW_UP_LIMIT (2000000000)


typedef struct {
    int idx;
    pthread_t pthread;
    int fd;
    char busy;
    char running;
    void* server;
} Client;

typedef struct {
    Cache* cache;
    int max_clients;
    Client* clients;
    int running;
} Server;

int running = 0;

void SigHandler(int id) {
    if (id == SIGINT) {
        running = 0;
    }
}

int ExtractUrl(char* part_url, char* full_url) {
    int get_prefix_size = strlen(GET_PREFIX);
    int full_url_size = strlen(full_url);

    if (full_url_size < get_prefix_size || memcmp(GET_PREFIX, full_url, get_prefix_size) != 0) {
        return 1;
    }

    full_url += get_prefix_size;
    char* url_without_http = strstr(full_url, HTTP_DELIM);
    if (url_without_http == NULL) {
        url_without_http = full_url;
    } else {
        url_without_http += strlen(HTTP_DELIM);
    }
    char* end = strstr(url_without_http, " HTTP");
    int part_url_size = end - url_without_http;
    memcpy(part_url, url_without_http, part_url_size);
    part_url[part_url_size] = '\0';
    return 0;
}

void ParseFullURL(char* host, char* path, int* port, char* full_url) {
    char* url_without_http = strstr(full_url, HTTP_DELIM);
    if (url_without_http == NULL) {
        url_without_http = full_url;
    } else {
        url_without_http += strlen(HTTP_DELIM);
    }
    char* url_tmp = strpbrk(url_without_http, ":/");
    // default port
    *port = PORT;
    if (url_tmp == NULL) {
        strcpy(path, "/");
        strcpy(host, url_without_http);
    } else {
        memcpy(host, url_without_http, url_tmp - url_without_http);
        host[url_tmp - url_without_http] = '\0';
        if (url_tmp[0] == '/') {
            strcpy(path, url_tmp);
        } else {
            sscanf(url_tmp, ":%99d/%99[^\n]", port, path);
        }
    }
}

int ReadFromHost(int host_socket, char** content) {
    int content_size = BUFSIZ;
    int read_count = 0;
    int read_iter = 1;
    *content = malloc(content_size * sizeof(char));
    if (*content == NULL) {
        return -1;
    }
    while (read_iter > 0) {
        if (content_size <= read_count) {
            int new_size = content_size + BUFSIZ;
            if (new_size <= EXP_GROW_UP_LIMIT) {
                new_size = content_size * 2;
            }
            *content = realloc(*content, new_size * sizeof(char));
            content_size = new_size;
        }
        read_iter = (int)read(host_socket, *content + read_count, content_size - read_count);
        read_count += read_iter;
    }
    return read_count;
}

int ConnectToHost(char* host, int port) {
    struct hostent *hp = gethostbyname(host);
    if (hp == NULL) {
        return UNABLE_TO_FIND_HOST;
    }
    struct sockaddr_in addr;
    memcpy(&addr.sin_addr, hp->h_addr, hp->h_length);
    addr.sin_port = htons(port);
    addr.sin_family = AF_INET;

    int sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == -1) {
        return UNABLE_TO_CREATE_SOCKET;
    }
    int opt_val = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char *)&opt_val, sizeof(int));

    int connection = connect(sock, (struct sockaddr *)&addr, sizeof(struct sockaddr_in));
    if (connection == -1) {
        return UNABLE_TO_CONNECT;
    }
    return sock;
}

int WriteToClient(int client_fd, const char* buff, int size) {
    int write_size = 0;
    while (write_size != size) {
        int write_iter = send(client_fd, buff + write_size, size, 0);
        if (write_iter < 1) {
            return 1;
        }
        write_size += write_iter;
    }
    return 0;
}

void HandleClientDisconnect(Client* client, Server* server) {
    printf("Client %d disconnected\n", client->idx);
    client->running = 0;
    shutdown(client->fd, SHUT_RDWR);
    close(client->fd);
    pthread_exit(NULL);
}

void* ClientWorker(void* arg) {
    printf("Handle client\n");

    Client* client = (Client *)arg;
    Server* server = (Server *)client->server;
    int client_fd = client->fd;

    struct pollfd poll_fd;
    poll_fd.fd = client_fd;
    poll_fd.events = POLLIN;

    char input_buffer[URL_BUFF_SIZE];
    while (server->running) {
        poll_fd.revents = 0;
        int poll_res = poll(&poll_fd, 1, POLL_TIMEOUT);

        if (poll_res == 0) {
            continue;
        }

        if (poll_res == -1 && errno != EINTR) {
            fprintf(stderr, "Error while poll\n");
            break;
        }

        if (poll_fd.revents & POLLIN) {
            int read_bytes = (int) read(client_fd, input_buffer, URL_BUFF_SIZE);
            if (read_bytes < 1) {
                HandleClientDisconnect(client, server);
            }
            input_buffer[read_bytes] = '\0';
            if ((int) input_buffer[0] == 0) {
                HandleClientDisconnect(client, server);
            }
            printf("Get request:\n%s\n", input_buffer);
            CacheRow *cache_row = GetCacheItem(server->cache, input_buffer, read_bytes);
            if (cache_row == NULL) {
                printf("Unable to find:\n%s\nin cache\n", input_buffer);
                char url[BUFSIZ];
                int err = ExtractUrl(url, input_buffer);
                if (err != 0) {
                    fprintf(stderr, "Unable to extract url from request\n");
                    HandleClientDisconnect(client, server);
                }
                char host[URL_BUFF_SIZE];
                char path[URL_BUFF_SIZE];
                int port;
                ParseFullURL(host, path, &port, url);
                printf("Parsed url: host=%s, path=%s, port=%d\n", host, path, port);
                int host_sock = ConnectToHost(host, port);
                printf("Connected to %s (port %d)\n", host, port);
                if (host_sock == UNABLE_TO_FIND_HOST) {
                    fprintf(stderr, "Unable to find host: %s\n", host);
                } else if (host_sock == UNABLE_TO_CONNECT) {
                    fprintf(stderr, "Unable to connect to host: %s\n", host);
                } else if (host_sock == UNABLE_TO_CREATE_SOCKET) {
                    fprintf(stderr, "Unable to create socket\n");
                } else {
                    err = WriteToClient(host_sock, input_buffer, read_bytes);
                    if (err != 0) {
                        fprintf(stderr, "Error while send request to host\n");
                        HandleClientDisconnect(client, server);
                    }
                    printf("Send GET request to %s\n", host);

                    char *content;
                    int content_size = ReadFromHost(host_sock, &content);
                    if (content_size < 0) {
                        fprintf(stderr, "Error while allocate memory for response\n");
                        HandleClientDisconnect(client, server);
                    }
                    close(host_sock);

                    // write to client
                    WriteToClient(client_fd, content, content_size);
                    if (err != 0) {
                        fprintf(stderr, "Error while write to client\n");
                        HandleClientDisconnect(client, server);
                    }
                    printf("Send %s response to client\n", host);

                    // write to cache
                    CacheItem *cache_item = malloc(sizeof(CacheItem));
                    if (cache_item == NULL) {
                        HandleClientDisconnect(client, server);
                    }
                    cache_item->content = content;
                    cache_item->last_visited = time(NULL);
                    cache_item->content_size = content_size;
                    cache_item->key = malloc((read_bytes + 1) * sizeof(char));
                    if (cache_item->key == NULL) {
                        fprintf(stderr, "Unable to allocate memory for cache row\n");
                    }
                    strcpy(cache_item->key, input_buffer);
                    cache_item->key_size = read_bytes;
                    int err = InsertCacheItem(server->cache, cache_item);
                    if (err != 0) {
                        fprintf(stderr, "Unable to save to cache\n");
                    }
                    printf("Save %s response to cache\n", host);

                    HandleClientDisconnect(client, server);
                }
            } else {
                printf("Find %s in cache\n", input_buffer);

                // get data from cache
                int err = WriteToClient(client_fd, cache_row->content, cache_row->content_size);
                if (err != 0) {
                    fprintf(stderr, "Error while write to client\n");
                    HandleClientDisconnect(client, server);
                }
                // cache_row allocates in GetCacheItem
                // So client responsible for that memory
                free(cache_row->content);
                free(cache_row);
            }
        }
    }

    HandleClientDisconnect(client, server);
    pthread_exit(NULL);
}

void* CleanerWorker(void* arg) {
    Server* server = (Server*)arg;
    while (server->running) {
        printf("Do cache cleanup...\n");
        sleep(CLEANUP_TIMEOUT);
        LookupAndClean(server->cache, CLEANUP_TIMEOUT);
    }
    pthread_exit(NULL);
}

int GetNotBusyClient(Client* clients, int max_clients) {
    for (int i = 0; i < max_clients; ++i) {
        if (!clients[i].busy) {
            return i;
        }
    }
    return -1;
}

void tryToJoinThreads(Server* server) {
    int max_clients = server->max_clients;
    Client* clients = server->clients;
    for (int i = 0; i < max_clients; ++i) {
        if (clients[i].busy && !clients[i].running) {
            int err = pthread_join(clients[i].pthread, NULL);
            if (err != 0) {
                fprintf(stderr, "Error while PTHREAD_JOIN(3) %d client thread. Error: %s\n", i, strerror(err));
            }
            clients[i].busy = 0;
        }
    }
}

int InitServer(Server* server, int max_clients, int cache_size, int cache_bucket_capacity) {
    server->max_clients = max_clients;
    server->running = 0;

    server->cache = (Cache *) malloc(sizeof(Cache));
    server->clients = (Client *)malloc(server->max_clients * sizeof(Client));
    if (server->cache == NULL || server->clients == NULL) {
        perror("Failed to allocate memory for clients and cache\n");
        return 1;
    }

    if (InitCache(server->cache, cache_size, cache_bucket_capacity) != 0) {
        perror("Error while init cache\n");
        return 1;
    }

    for (int i = 0; i < server->max_clients; ++i) {
        server->clients[i].idx = i;
        server->clients[i].busy = 0;
        server->clients[i].running = 0;
        server->clients[i].server = server;
    }

    return 0;
}

int RunServer(Server* server, int port) {

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(struct sockaddr_in));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    printf("Creating listening socket...\n");
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1){
        perror("Error in SOCKET(2)!\n");
        return 1;
    }

    printf("Binding...\n");
    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        perror("Error in BIND(2)!\n");
        return 1;
    }

    if (listen(listen_fd, server->max_clients) != 0) {
        perror("Error in LISTEN(2)\n");
        return 1;
    }
    printf("Server redy to listen\n");

    struct pollfd poll_fd;
    poll_fd.fd = listen_fd;
    poll_fd.events = POLLIN;

    running = 1;
    server->running = 1;
    printf("Server starts\n");

    pthread_t cleaner_thread;
    int err = pthread_create(&cleaner_thread, NULL, CleanerWorker, (void *) (server));
    if (err != 0) {
        fprintf(stderr, "Error in PTHREAD_CREATE(3C). Error: %s\n", strerror(err));
        running = 0;
        server->running = 0;
        return 1;
    }

    while (running) {
        poll_fd.revents = 0;
        int poll_res = poll(&poll_fd, 1, POLL_TIMEOUT);

        if (poll_res == 0) {
            continue;
        }

        if (poll_res == -1 && errno != EINTR) {
            fprintf(stderr, "Error while poll\n");
            break;
        }

        if (poll_fd.revents & POLLIN) {
            int new_client_fd = accept(listen_fd, NULL, NULL);
            if (new_client_fd == -1) {
                perror("Error in ACCEPT(2)");
                continue;
            }


            tryToJoinThreads(server);
            int client_id = GetNotBusyClient(server->clients, server->max_clients);
            if (client_id == -1) {
                shutdown(new_client_fd, SHUT_RDWR);
                close(new_client_fd);
                perror("Unable to accept more clients\n");
                continue;
            }

            printf("Client %d connected\n", client_id);
            server->clients[client_id].busy = 1;
            server->clients[client_id].running = 1;
            server->clients[client_id].fd = new_client_fd;
            err = pthread_create(
                    &(server->clients[client_id].pthread),
                    NULL,
                    ClientWorker,
                    (void *)(&(server->clients[client_id]))
            );
            if (err != 0) {
                server->clients[client_id].busy = 0;
                server->clients[client_id].running = 0;
                fprintf(stderr, "Error while create thread\n");
            }
        }
    }
    printf("\nGraceful shutdown:\n");

    // To stop all threads that is running til server running (every thread)
    server->running = 0;

    printf("Joining clients threads...\n");
    tryToJoinThreads(server);
    printf("Joining cleaner thread...\n");
    pthread_join(cleaner_thread, NULL);

    printf("Free memory...\n");
    DeleteCache(server->cache);
    free(server->clients);

    printf("Close socket...\n");
    close(listen_fd);

    return 0;
}

int main(int argc, char* argv[]) {
    if (argc != 5) {
        printf("Not enough args\nUsege: %s <server port> <max clients> <cache capacity> <cache bucket capacity>\n", argv[0]);
        pthread_exit(NULL);
    }

    int port = atoi(argv[1]);
    int max_clients = atoi(argv[2]);
    int cache_size = atoi(argv[3]);
    int cache_bucket_capacity = atoi(argv[4]);

    struct sigaction sa;
    sa.sa_handler = SigHandler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);

    Server server;
    if (InitServer(&server, max_clients, cache_size, cache_bucket_capacity) != 0) {
        fprintf(stderr, "Unable to init server. Exiting\n");
        return 1;
    }

    if (RunServer(&server, port) != 0) {
        fprintf(stderr, "Error while run server. Free memory. Exiting\n");
        DeleteCache(server.cache);
        free(server.clients);
        return 1;
    }

    return 0;
}