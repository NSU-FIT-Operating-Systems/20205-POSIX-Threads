#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <malloc.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <poll.h>
#include <stdbool.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <sys/eventfd.h>

#include "cache.h"
#include "picohttpparser-master/picohttpparser.h"
#include "sync_pipe/sync_pipe.h"

#define ERROR_INVALID_ARGS 1
#define ERROR_PORT_CONVERSATION 2
#define ERROR_ALLOC 3
#define ERROR_SOCKET_INIT 4
#define ERROR_BIND 5
#define ERROR_LISTEN 6
#define ERROR_PIPE_OPEN 7
#define ERROR_SIG_HANDLER_INIT 8
#define ERROR_INIT_SYNC_PIPE 9

#define TIMEOUT 1200
#define START_REQUEST_SIZE BUFSIZ
#define START_RESPONSE_SIZE BUFSIZ
#define LISTEN_NUM 30

pthread_mutex_t num_threads_mutex;
bool valid_num_threads_mutex = false;
pthread_cond_t stop_cond;
bool valid_stop_cond = false;

int num_threads = 0;
pthread_attr_t thread_attr;
bool valid_thread_attr = false;
bool is_stop = false;


int WRITE_STOP_FD = -1;
int READ_STOP_FD = -1;

void destroyPollFds(struct pollfd *poll_fds, int *poll_last_index) {
    for (int i = 0; i < *poll_last_index; i++) {
        if (poll_fds[i].fd > 0) {
            //fprintf(stderr, "closing fd %d...\n", poll_fds[i].fd);
            int close_res = close(poll_fds[i].fd);
            if (close_res < 0) {
                fprintf(stderr, "error while closing fd %d ", poll_fds[i].fd);
                perror("close");
            }
            poll_fds[i].fd = -1;
        }
    }
    free(poll_fds);
    *poll_last_index = -1;
}

void removeFromPollFds(struct pollfd * poll_fds, int *poll_last_index, int fd) {
    if (fd < 0) {
        return;
    }
    int i;
    for (i = 0; i < *poll_last_index; i++) {
        if (poll_fds[i].fd == fd) {
            close(poll_fds[i].fd);
            poll_fds[i].fd = -1;
            poll_fds[i].events = 0;
            poll_fds[i].revents = 0;
            break;
        }
    }
    if (i == *poll_last_index - 1) {
        *poll_last_index -= 1;
    }
    for (i = (int)*poll_last_index - 1; i > 0; i--) {
        if (poll_fds[i].fd == -1) {
            *poll_last_index -= 1;
        }
        else {
            break;
        }
    }
}

cache_list_t *cache_list = NULL;
bool valid_cache = false;
pthread_rwlockattr_t rw_lock_attr;
bool valid_rw_lock_attr = false;

void destroyRwLockAttr() {
    if (valid_rw_lock_attr) {
        pthread_rwlockattr_destroy(&rw_lock_attr);
        valid_rw_lock_attr = false;
    }
}

void destroyCacheList() {
    destroyList(cache_list);
    valid_cache = false;
}

void initStopCond() {
    if (!valid_stop_cond) {
        pthread_cond_init(&stop_cond, NULL);
        valid_stop_cond = true;
    }
}

void destroyStopCond() {
    if (valid_stop_cond) {
        pthread_cond_destroy(&stop_cond);
        valid_stop_cond = false;
    }
}

typedef struct client {
    char *request;
    cache_t *cache_record;
    size_t REQUEST_SIZE;
    size_t request_index;
    bool is_stop;
    int write_response_index;
    int fd;
} client_t;

typedef struct server {
    cache_t *cache_record;
    int write_request_index;
    bool is_stop;
    int fd;
} server_t;

void cleanUp() {
    fprintf(stderr, "\ncleaning up...\n");
    is_stop = true;
    sync_pipe_notify(num_threads * 2);
    pthread_mutex_lock(&num_threads_mutex);
    while (num_threads != 0) {
        pthread_cond_wait(&stop_cond, &num_threads_mutex);
    }
    pthread_mutex_unlock(&num_threads_mutex);
    if (WRITE_STOP_FD != -1) {
        close(WRITE_STOP_FD);
        WRITE_STOP_FD = -1;
    }
    if (valid_cache) {
        destroyCacheList();
    }
    if (valid_rw_lock_attr) {
        destroyRwLockAttr();
    }
    if (valid_num_threads_mutex) {
        pthread_mutex_destroy(&num_threads_mutex);
        valid_num_threads_mutex = false;
    }
    if (valid_thread_attr) {
        pthread_attr_destroy(&thread_attr);
        valid_thread_attr = false;
    }
    destroyStopCond();
    sync_pipe_close();

}

void initThreadAttr() {
    if (!valid_thread_attr) {
        pthread_attr_init(&thread_attr);
        pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_DETACHED);
        valid_thread_attr = true;
    }
}

void initRwLockAttr() {
    if (!valid_rw_lock_attr) {
        pthread_rwlockattr_init(&rw_lock_attr);
        pthread_rwlockattr_setkind_np(&rw_lock_attr, PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP);
        valid_rw_lock_attr = true;
    }
}

void initEmptyCacheRecord(cache_t *record) {
    if (record == NULL) {
        return;
    }
    record->request = NULL;
    record->response = NULL;
    record->response_index = 0;
    record->RESPONSE_SIZE = 0;
    record->full = false;
    record->url = NULL;
    record->URL_LEN = 0;
    record->num_subscribers = 0;
    record->event_fd = eventfd(0, EFD_NONBLOCK | EFD_SEMAPHORE);
    record->server_alive = -1;
    record->private = true;
    pthread_rwlock_init(&record->rw_lock, &rw_lock_attr);
    record->valid_rw_lock = true;
    pthread_mutex_init(&record->subs_mutex, NULL);
    record->valid_subs_mutex = true;
    record->valid = false;
}

void initCacheList() {
    cache_list = initList();
    if (cache_list == NULL) {
        cleanUp();
        return;
    }
    valid_cache = true;
}

struct pollfd *initPollFds(size_t POLL_TABLE_SIZE, int *poll_last_index) {
    struct pollfd *poll_fds = (struct pollfd *)calloc(POLL_TABLE_SIZE, sizeof(struct pollfd));
    if (poll_fds == NULL) {
        fprintf(stderr, "failed to alloc memory for poll_fds\n");
        return NULL;
    }
    for (int i = 0; i < POLL_TABLE_SIZE; i++) {
        poll_fds[i].fd = -1;
    }
    *poll_last_index = 0;
    return poll_fds;
}

void reallocPollFds(struct pollfd **poll_fds, size_t *POLL_TABLE_SIZE) {
    size_t prev_size = *POLL_TABLE_SIZE;
    *POLL_TABLE_SIZE *= 2;
    *poll_fds = realloc(*poll_fds, *POLL_TABLE_SIZE * (sizeof(struct pollfd)));
    for (size_t i = prev_size; i < *POLL_TABLE_SIZE; i++) {
        (*poll_fds)[i].fd = -1;
    }
}

void addFdToPollFds(struct pollfd **poll_fds, int *poll_last_index, size_t *POLL_TABLE_SIZE, int fd, short events) {
    if (fd < 0) {
        return;
    }
    for (int i = 0; i < *poll_last_index; i++) {
        if ((*poll_fds)[i].fd == -1) {
            (*poll_fds)[i].fd = fd;
            (*poll_fds)[i].events = events;
            return;
        }
    }
    if (*poll_last_index >= *POLL_TABLE_SIZE) {
        reallocPollFds(poll_fds, POLL_TABLE_SIZE);
    }
    (*poll_fds)[*poll_last_index].fd = fd;
    (*poll_fds)[*poll_last_index].events = events;
    *poll_last_index += 1;
}

int connectToServerHost(char *hostname, int port) {
    if (hostname == NULL || port < 0) {
        return -1;
    }
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        return -1;
    }

    struct hostent *h = gethostbyname(hostname);
    if (h == NULL) {
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_port = htons(port);
    addr.sin_family = AF_INET;
    memcpy(&addr.sin_addr, h->h_addr, h->h_length);

    int connect_res = connect(server_sock, (struct sockaddr*)&addr, sizeof(struct sockaddr_in));
    if (connect_res != 0) {
        perror("connect");
        return -1;
    }
    return server_sock;
}

int initListener(int port) {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        cleanUp();
        exit(ERROR_SOCKET_INIT);
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(struct sockaddr_in));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    int bind_res = bind(listen_fd, (struct sockaddr*)&addr, sizeof(struct sockaddr_in));
    if (bind_res != 0) {
        perror("bind");
        close(listen_fd);
        cleanUp();
        exit(ERROR_BIND);
    }

    int listen_res = listen(listen_fd, LISTEN_NUM);
    if (listen_res == -1) {
        perror("listen");
        close(listen_fd);
        cleanUp();
        exit(ERROR_LISTEN);
    }
    return listen_fd;
}

int createThread(bool is_client, void *arg);

void acceptNewClient(int listen_fd) {
    int new_client_fd = accept(listen_fd, NULL, NULL);
    if (new_client_fd == -1) {
        perror("new client accept");
        return;
    }
    int fcntl_res = fcntl(new_client_fd, F_SETFL, O_NONBLOCK);
    if (fcntl_res < 0) {
        perror("make new client nonblock");
        close(new_client_fd);
        return;
    }
    client_t *client = (client_t *) calloc(1, sizeof(client_t));
    if (client == NULL) {
        close(new_client_fd);
        pthread_exit((void *) ERROR_ALLOC);
    }
    client->fd = new_client_fd;
    client->cache_record = NULL;
    client->request = NULL;
    client->REQUEST_SIZE = 0;
    client->request_index = 0;
    client->write_response_index = -1;
    client->is_stop = false;
    fprintf(stderr, "new client with fd %d accepted\n", new_client_fd);
    int res = createThread(true, (void *)client);
    if (res != 0) {
        fprintf(stderr, "error starting thread for client with fd %d\n", new_client_fd);
        close(new_client_fd);
        free(client);
    }
}

void changeEventForFd(struct pollfd *poll_fds, const int *poll_last_index, int fd, short new_events) {
    if (fd < 0) {
        return;
    }
    for (int i = 0; i < *poll_last_index; i++) {
        if (poll_fds[i].fd == fd) {
            poll_fds[i].events = new_events;
            return;
        }
    }
}

void removeSubscriber(cache_t *record) {
    if (record == NULL) {
        return;
    }
    pthread_mutex_lock(&record->subs_mutex);
    if (record->num_subscribers > 0) {
        record->num_subscribers -= 1;
    }
    pthread_mutex_unlock(&record->subs_mutex);
}

void disconnectClient(client_t *client) {
    if (client == NULL) {
        return;
    }
    client->is_stop = true;
    fprintf(stderr, "disconnecting client with fd %d...\n", client->fd);
}

void addSubscriber(cache_t *record, struct pollfd **poll_fds, int *poll_last_index, size_t *POLL_TABLE_SIZE) {
    if (record == NULL || !record->valid) {
        return;
    }
    pthread_mutex_lock(&record->subs_mutex);
    record->num_subscribers += 1;
    pthread_mutex_unlock(&record->subs_mutex);
    addFdToPollFds(poll_fds, poll_last_index, POLL_TABLE_SIZE, record->event_fd, POLLIN);
}

void notifySubscribers(cache_t *record) {
    pthread_mutex_lock(&record->subs_mutex);
    uint64_t u = record->num_subscribers;
    pthread_mutex_unlock(&record->subs_mutex);
    write(record->event_fd, &u, sizeof(uint64_t));
}

void disconnectServer(server_t *server) {
    if (server == NULL) {
        return;
    }
    server->is_stop = true;
    fprintf(stderr, "disconnect server with fd %d...\n", server->fd);
}

void printCacheRecord(cache_t *record) {
    if (record == NULL) {
        fprintf(stderr, "cache record is NULL\n");
        return;
    }
    fprintf(stderr, "cache record:\n");
    if (record->valid) {
        fprintf(stderr, "valid, ");
    }
    else {
        fprintf(stderr, "NOT valid, ");
    }
    if (record->private) {
        fprintf(stderr, "private, ");
    }
    else {
        fprintf(stderr, "public, ");
    }
    if (record->full) {
        fprintf(stderr, "full\n");
    }
    else {
        fprintf(stderr, "NOT full\n");
    }
    fprintf(stderr, "server_alive = %d\n", record->server_alive);
    fprintf(stderr, "REQ_SIZE = %lu\n", record->REQUEST_SIZE);
    if (record->valid_rw_lock) {
        fprintf(stderr, "rw_lock = valid, ");
    }
    else {
        fprintf(stderr, "rw_lock = NOT valid, ");
    }
    fprintf(stderr, "rsp_ind = %lu, RSP_SIZE = %lu\n", record->response_index, record->RESPONSE_SIZE);
    if (record->valid_subs_mutex) {
        fprintf(stderr, "subs_mutex = valid, ");
        pthread_mutex_lock(&record->subs_mutex);
        fprintf(stderr, "num_subs = %d\n\n", record->num_subscribers);
        pthread_mutex_unlock(&record->subs_mutex);
    }
    else {
        fprintf(stderr, "subs_mutex = NOT valid, ");
        fprintf(stderr, "SUBS_SIZE = %d\n\n", record->num_subscribers);
    }
}

void printCacheList() {
    pthread_mutex_lock(&cache_list->mutex);
    cache_node_t *list_nodes = cache_list->head;
    fprintf(stderr, "printing cache...\n");
    while (list_nodes != NULL) {
        printCacheRecord(list_nodes->record);
        list_nodes = list_nodes->next;
    }
    pthread_mutex_unlock(&cache_list->mutex);
}

void findAndAddCacheRecord(char *url, size_t url_len, client_t *client, char *host, size_t REQUEST_SIZE,
                           struct pollfd **poll_fds, int *poll_last_index, size_t *POLL_TABLE_SIZE) {
    pthread_mutex_lock(&cache_list->mutex);
    cache_node_t *list_nodes = cache_list->head;
    cache_node_t *prev_node = NULL;
    while (list_nodes != NULL) {
        if (!list_nodes->record->valid) {
            cache_node_t *next_node = list_nodes->next;
            freeCacheRecord(list_nodes->record);
            free(list_nodes->record);
            free(list_nodes);
            list_nodes = next_node;
            continue;
        }
        if (list_nodes->record->URL_LEN == url_len && strncmp(list_nodes->record->url, url, url_len) == 0) {
            client->cache_record = list_nodes->record;
            addSubscriber(list_nodes->record, poll_fds, poll_last_index, POLL_TABLE_SIZE);
            client->write_response_index = 0;
            pthread_rwlock_rdlock(&client->cache_record->rw_lock);
            if (client->cache_record->response_index != 0) {
                changeEventForFd(*poll_fds, poll_last_index, client->fd, POLLIN | POLLOUT);
            }
            pthread_rwlock_unlock(&client->cache_record->rw_lock);
            pthread_mutex_unlock(&cache_list->mutex);
            return;
        }
        pthread_mutex_unlock(&cache_list->mutex);

        pthread_mutex_lock(&cache_list->mutex);
        prev_node = list_nodes;
        list_nodes = list_nodes->next;
    }
    cache_node_t *new_node = (cache_node_t *) calloc(1, sizeof(cache_node_t));
    if (new_node == NULL) {
        pthread_mutex_unlock(&cache_list->mutex);
        fprintf(stderr, "failed to add client with fd %d to cache (can't create new node)\n", client->fd);
        disconnectClient(client);
        return;
    }
    new_node->record = (cache_t *)calloc(1, sizeof(cache_t));
    if (new_node->record == NULL) {
        pthread_mutex_unlock(&cache_list->mutex);
        fprintf(stderr, "failed to add client with fd %d to cache (can't create new record)\n", client->fd);
        free(new_node);
        disconnectClient(client);
        return;
    }
    new_node->next = NULL;
    initEmptyCacheRecord(new_node->record);
    new_node->record->url = url;
    new_node->record->URL_LEN = url_len;
    new_node->record->valid = true;
    if (prev_node == NULL) {
        cache_list->head = new_node;
    }
    else {
        prev_node->next = new_node;
    }
    pthread_mutex_unlock(&cache_list->mutex);

    new_node->record->request = (char *)calloc(REQUEST_SIZE, sizeof(char));
    if (new_node->record->request == NULL) {
        freeCacheRecord(new_node->record);
        disconnectClient(client);
        return;
    }
    memcpy(new_node->record->request, client->request, REQUEST_SIZE);
    new_node->record->REQUEST_SIZE = REQUEST_SIZE;
    int server_fd = connectToServerHost(host, 80);
    if (server_fd == -1) {
        fprintf(stderr, "failed to connect to remote host: %s\n", host);
        freeCacheRecord(new_node->record);
        disconnectClient(client);
        free(host);
        return;
    }
    free(host);
    int fcntl_res = fcntl(server_fd, F_SETFL, O_NONBLOCK);
    if (fcntl_res < 0) {
        perror("make new server fd nonblock");
        close(server_fd);
        freeCacheRecord(new_node->record);
        disconnectClient(client);
        return;
    }
    server_t *server = (server_t *) calloc(1, sizeof(server_t));
    if (server == NULL) {
        fprintf(stderr, "failed to alloc memory for new server\n");
        close(server_fd);
        freeCacheRecord(new_node->record);
        disconnectClient(client);
        return;
    }
    server->fd = server_fd;
    server->cache_record = new_node->record;
    server->write_request_index = 0;
    server->is_stop = false;
    int res = createThread(false, (void *)server);
    if (res != 0) {
        fprintf(stderr, "error starting server thread with server fd %d by client with fd %d\n", server_fd, client->fd);
        close(server_fd);
        free(server);
        freeCacheRecord(new_node->record);
        disconnectClient(client);
        return;
    }

    client->cache_record = new_node->record;
    addSubscriber(new_node->record, poll_fds, poll_last_index, POLL_TABLE_SIZE);
    client->write_response_index = 0;
}

void shiftRequest(char **request, size_t *request_index, int pret) {
    if (*request == NULL || *request_index == 0 || pret <= 0) {
        return;
    }
    for (int i = pret; i < *request_index; i++) {
        (*request)[i] = (*request)[i - pret];
    }
    memset(&(*request)[*request_index - pret], 0, pret);
    *request_index -= pret;
}

void readFromClient(client_t *client, struct pollfd **poll_fds, int *poll_last_index, size_t *POLL_TABLE_SIZE) {
    if (client->fd == -1 || client->is_stop) {
        return;
    }
    char buf[BUFSIZ];
    ssize_t was_read = read(client->fd, buf, BUFSIZ);
    if (was_read < 0) {
        fprintf(stderr, "read from client with fd %d ", client->fd);
        perror("read");
        disconnectClient(client);
        return;
    }
    else if (was_read == 0) {
        fprintf(stderr, "client with fd %d closed connection\n", client->fd);
        disconnectClient(client);
        return;
    }
    if (client->REQUEST_SIZE <= 0) {
        client->REQUEST_SIZE = START_REQUEST_SIZE;
        client->request = (char *)calloc(client->REQUEST_SIZE, sizeof(char));
        if (client->request == NULL) {
            fprintf(stderr, "calloc returned NULL\n");
            disconnectClient(client);
            return;
        }
    }
    if (client->request_index + was_read >= client->REQUEST_SIZE) {
        client->REQUEST_SIZE *= 2;
        client->request = realloc(client->request, client->REQUEST_SIZE * sizeof(char));
    }
    memcpy(&client->request[client->request_index], buf, was_read);
    client->request_index += was_read;
    char *method;
    char *path;
    size_t method_len, path_len;
    int minor_version;
    size_t num_headers = 100;
    struct phr_header headers[num_headers];
    int pret = phr_parse_request(client->request, client->request_index,
                                 (const char **)&method, &method_len, (const char **)&path, &path_len,
                                 &minor_version, headers, &num_headers, 0);
    if (pret > 0) {
        if (strncmp(method, "GET", method_len) != 0) {
            disconnectClient(client);
            return;
        }
        size_t url_len = path_len;
        char *url = calloc(url_len, sizeof(char));
        if (url == NULL) {
            disconnectClient(client);
            return;
        }
        memcpy(url, path, path_len);


        char *host = NULL;
        for (size_t i = 0; i < num_headers; i++) {
            if (strncmp(headers[i].name, "Host", 4) == 0) {
                host = calloc(headers[i].value_len + 1, sizeof(char));
                if (host == NULL) {
                    free(url);
                    disconnectClient(client);
                    return;
                }
                memcpy(host, headers[i].value, headers[i].value_len);
                break;
            }
        }
        if (host == NULL) {
            free(url);
            disconnectClient(client);
            return;
        }
        findAndAddCacheRecord(url, url_len, client, host, pret, poll_fds, poll_last_index, POLL_TABLE_SIZE);
        shiftRequest(&client->request, &client->request_index, pret);
    }
    else if (pret == -1) {
        disconnectClient(client);
    }
}


void writeToServer(server_t *server, struct pollfd *poll_fds, int *poll_last_index) {
    if (server == NULL || server->fd < 0 || server->is_stop) {
        return;
    }
    ssize_t written = write(server->fd,
                            &server->cache_record->request[server->write_request_index],
                            server->cache_record->REQUEST_SIZE - server->write_request_index);
    if (written < 0) {
        fprintf(stderr, "write to server with fd %d ", server->fd);
        perror("write");
        disconnectServer(server);
        notifySubscribers(server->cache_record);
        return;
    }
    server->write_request_index += (int)written;
    if (server->write_request_index == server->cache_record->REQUEST_SIZE) {
        changeEventForFd(poll_fds, poll_last_index, server->fd, POLLIN);
    }
}

void readFromServer(server_t *server) {
    if (server == NULL || server->fd < 0 || server->is_stop) {
        return;
    }
    char buf[BUFSIZ];
    ssize_t was_read = read(server->fd, buf, BUFSIZ);
    if (was_read < 0) {
        fprintf(stderr, "read from server with fd %d ", server->fd);
        perror("read");
        disconnectServer(server);
        notifySubscribers(server->cache_record);
        return;
    }
    else if (was_read == 0) {
        server->cache_record->full = true;
        pthread_rwlock_wrlock(&server->cache_record->rw_lock);

        server->cache_record->response = realloc(
                server->cache_record->response,
                server->cache_record->response_index * sizeof(char));
        server->cache_record->RESPONSE_SIZE = server->cache_record->response_index;

        pthread_rwlock_unlock(&server->cache_record->rw_lock);
        notifySubscribers(server->cache_record);
        disconnectServer(server);
        return;
    }
    pthread_rwlock_wrlock(&server->cache_record->rw_lock);
    if (server->cache_record->RESPONSE_SIZE == 0) {
        server->cache_record->RESPONSE_SIZE = START_RESPONSE_SIZE;
        server->cache_record->response = (char *)calloc(START_RESPONSE_SIZE, sizeof(char));
        if (server->cache_record->response == NULL) {
            pthread_rwlock_unlock(&server->cache_record->rw_lock);
            disconnectServer(server);
            notifySubscribers(server->cache_record);
            return;
        }
    }
    if (was_read + server->cache_record->response_index >=
        server->cache_record->RESPONSE_SIZE) {
        server->cache_record->RESPONSE_SIZE *= 2;
        server->cache_record->response = realloc(
                server->cache_record->response,
                server->cache_record->RESPONSE_SIZE * sizeof(char));
    }
    memcpy(&server->cache_record->response[server->cache_record->response_index],
           buf, was_read);
    size_t prev_len = server->cache_record->response_index;
    server->cache_record->response_index += was_read;
    pthread_rwlock_unlock(&server->cache_record->rw_lock);
    int minor_version, status;
    char *msg;
    size_t msg_len;
    size_t num_headers = 100;
    struct phr_header headers[num_headers];
    pthread_rwlock_rdlock(&server->cache_record->rw_lock);
    int pret = phr_parse_response(server->cache_record->response, server->cache_record->response_index,
                                  &minor_version, &status, (const char **)&msg, &msg_len, headers,
                                  &num_headers, prev_len);
    pthread_rwlock_unlock(&server->cache_record->rw_lock);
    notifySubscribers(server->cache_record);
    if (pret > 0) {
        if (status >= 200 && status < 300) {
            server->cache_record->private = false;
        }
    }
}


void writeToClient(client_t *client, struct pollfd *poll_fds, int *poll_last_index) {
    if (client == NULL || client->fd < 0 || client->is_stop) {
        fprintf(stderr, "invalid client\n");
        return;
    }
    if (client->cache_record == NULL) {
        fprintf(stderr, "client with fd %d cache record is NULL\n", client->fd);
        disconnectClient(client);
        return;
    }
    if (!client->cache_record->server_alive && !client->cache_record->full) {
        freeCacheRecord(client->cache_record);
        disconnectClient(client);
        return;
    }
    pthread_rwlock_rdlock(&client->cache_record->rw_lock);
    ssize_t written = write(client->fd, &client->cache_record->response[client->write_response_index],
                            client->cache_record->response_index - client->write_response_index);
    pthread_rwlock_unlock(&client->cache_record->rw_lock);
    if (written < 0) {
        fprintf(stderr, "write to client with fd %d ", client->fd);
        perror("write");
        disconnectClient(client);
        return;
    }
    client->write_response_index += (int)written;
    if (client->write_response_index == client->cache_record->response_index) {
        changeEventForFd(poll_fds, poll_last_index, client->fd, POLLIN);
    }
}

static void sigCatch(int sig) {
    if (sig == SIGINT) {
        if (WRITE_STOP_FD != -1) {
            char a = 'a';
            write(WRITE_STOP_FD, &a, 1);
            close(WRITE_STOP_FD);
            WRITE_STOP_FD = -1;
        }
    }
}

void *clientThread(void *arg) {
    client_t *client = (client_t *)arg;

    size_t POLL_TABLE_SIZE = 3;
    int poll_last_index = -1;

    struct pollfd *poll_fds = initPollFds(POLL_TABLE_SIZE, &poll_last_index);
    if (poll_fds == NULL) {
        close(client->fd);
        pthread_exit((void *) ERROR_ALLOC);
    }
    addFdToPollFds(&poll_fds, &poll_last_index, &POLL_TABLE_SIZE, get_rfd_spipe(), POLLIN);
    addFdToPollFds(&poll_fds, &poll_last_index, &POLL_TABLE_SIZE, client->fd, POLLIN);

    pthread_mutex_lock(&num_threads_mutex);
    num_threads += 1;
    pthread_mutex_unlock(&num_threads_mutex);

    while (!is_stop && !client->is_stop) {
        int poll_res = poll(poll_fds, poll_last_index, TIMEOUT * 1000);
        if (is_stop || client->is_stop) {
            break;
        }
        if (poll_res < 0) {
            perror("poll");
            break;
        }
        else if (poll_res == 0) {
            fprintf(stdout, "proxy timeout\n");
            break;
        }
        int num_handled_fd = 0;
        size_t i = 0;
        size_t prev_last_index = poll_last_index;
        while (num_handled_fd < poll_res && i < prev_last_index && !is_stop && !client->is_stop) {
            if (poll_fds[i].fd == get_rfd_spipe() && (poll_fds[i].revents & POLLIN)) {
                char s;
                read(poll_fds[i].fd, &s, 1);
                is_stop = true;
                break;
            }
            bool handled = false;
            if (poll_fds[i].fd == client->fd && (poll_fds[i].revents & POLLIN)) {
                readFromClient(client, &poll_fds, &poll_last_index, &POLL_TABLE_SIZE);
                handled = true;
            }
            if (poll_fds[i].fd == client->fd && (poll_fds[i].revents & POLLOUT) && !client->is_stop) {
                writeToClient(client, poll_fds, &poll_last_index);
                handled = true;
            }
            if (poll_fds[i].fd != client->fd && (poll_fds[i].revents & POLLIN)) {
                uint64_t u;
                ssize_t read_res = read(poll_fds[i].fd, &u, sizeof(u));
                if (u != 0 && read_res >= 0) {
                    pthread_rwlock_rdlock(&client->cache_record->rw_lock);
                    if (client->cache_record->response_index > client->write_response_index) {
                        changeEventForFd(poll_fds, &poll_last_index, client->fd, POLLIN | POLLOUT);
                    }
                    pthread_rwlock_unlock(&client->cache_record->rw_lock);
                }
                handled = true;
            }
            i += 1;
            if (handled) {
                num_handled_fd += 1;
            }
        }
    }

    removeFromPollFds(poll_fds, &poll_last_index, client->fd);
    if (client->request != NULL) {
        free(client->request);
    }
    free(client);
    free(poll_fds);
    pthread_mutex_lock(&num_threads_mutex);
    num_threads -= 1;
    fprintf(stderr, "num_threads = %d\n", num_threads);
    pthread_cond_signal(&stop_cond);
    pthread_mutex_unlock(&num_threads_mutex);
    pthread_exit((void *)0);
}

void *serverThread(void *arg) {
    server_t *server = (server_t *)arg;
    size_t POLL_TABLE_SIZE = 2;
    int poll_last_index = -1;

    struct pollfd *poll_fds = initPollFds(POLL_TABLE_SIZE, &poll_last_index);
    if (poll_fds == NULL) {
        close(server->fd);
        free(server);
        pthread_exit((void *) ERROR_ALLOC);
    }
    addFdToPollFds(&poll_fds, &poll_last_index, &POLL_TABLE_SIZE, get_rfd_spipe(), POLLIN);
    addFdToPollFds(&poll_fds, &poll_last_index, &POLL_TABLE_SIZE, server->fd, POLLIN | POLLOUT);

    pthread_mutex_lock(&num_threads_mutex);
    num_threads += 1;
    pthread_mutex_unlock(&num_threads_mutex);

    while (!is_stop && !server->is_stop) {
        int poll_res = poll(poll_fds, poll_last_index, TIMEOUT * 1000);
        if (is_stop || server->is_stop) {
            break;
        }
        if (poll_res < 0) {
            perror("poll");
            break;
        }
        else if (poll_res == 0) {
            fprintf(stdout, "proxy timeout\n");
            break;
        }
        int num_handled_fd = 0;
        size_t i = 0;
        size_t prev_last_index = poll_last_index;
        while (num_handled_fd < poll_res && i < prev_last_index && !is_stop && !server->is_stop) {
            if (poll_fds[i].fd == get_rfd_spipe() && (poll_fds[i].revents & POLLIN)) {
                char s;
                read(poll_fds[i].fd, &s, 1);
                is_stop = true;
                break;
            }
            bool handled = false;
            if (poll_fds[i].fd == server->fd && (poll_fds[i].revents & POLLIN)) {
                readFromServer(server);
                handled = true;
            }
            if (poll_fds[i].fd == server->fd && (poll_fds[i].revents & POLLOUT) && !server->is_stop) {
                writeToServer(server, poll_fds, &poll_last_index);
                handled = true;
            }
            i += 1;
            if (handled) {
                num_handled_fd += 1;
            }
        }
    }
    if (server->cache_record != NULL) {
        server->cache_record->server_alive = false;
        fprintf(stderr, "server with fd %d was working with url: %s\n", server->fd, server->cache_record->url);
    }
    removeFromPollFds(poll_fds, &poll_last_index, server->fd);
    free(server);
    free(poll_fds);
    pthread_mutex_lock(&num_threads_mutex);
    num_threads -= 1;
    fprintf(stderr, "num_threads = %d\n", num_threads);
    pthread_cond_signal(&stop_cond);
    pthread_mutex_unlock(&num_threads_mutex);
    pthread_exit((void *)0);
}

int createThread(bool is_client, void *arg) {
    sigset_t old_set;
    sigset_t thread_set;
    sigemptyset(&thread_set);
    sigaddset(&thread_set, SIGINT);
    int create_res = 0;
    pthread_t tid;
    pthread_sigmask(SIG_BLOCK, &thread_set, &old_set);
    if (is_client) {
        create_res = pthread_create(&tid, &thread_attr, clientThread, arg);
    }
    else {
        create_res = pthread_create(&tid, &thread_attr, serverThread, arg);
    }
    pthread_sigmask(SIG_SETMASK, &old_set, NULL);
    return create_res;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Error wrong amount of arguments\n");
        exit(ERROR_INVALID_ARGS);
    }
    char *invalid_sym;
    errno = 0;
    int port = (int)strtol(argv[1], &invalid_sym, 10);
    if (errno != 0 || *invalid_sym != '\0') {
        fprintf(stderr, "Error wrong port\n");
        exit(ERROR_PORT_CONVERSATION);
    }

    initStopCond();

    int sync_pipe_res = sync_pipe_init();
    if (sync_pipe_res != 0) {
        fprintf(stderr, "failed to init sync pipe\n");
        cleanUp();
        exit(ERROR_INIT_SYNC_PIPE);
    }
    size_t POLL_TABLE_SIZE = 2;
    int poll_last_index = -1;

    struct pollfd *poll_fds = initPollFds(POLL_TABLE_SIZE, &poll_last_index);
    if (poll_fds == NULL) {
        fprintf(stderr, "failed to alloc memory for poll_fds\n");
        cleanUp();
        exit(ERROR_ALLOC);
    }

    int pipe_fds[2];
    int pipe_res = pipe(pipe_fds);
    if (pipe_res != 0) {
        perror("pipe:");
        exit(ERROR_PIPE_OPEN);
    }
    READ_STOP_FD = pipe_fds[0];
    WRITE_STOP_FD = pipe_fds[1];
    addFdToPollFds(&poll_fds, &poll_last_index, &POLL_TABLE_SIZE, READ_STOP_FD, POLLIN);

    struct sigaction sig_act = { 0 };
    sig_act.sa_handler = sigCatch;
    sigemptyset(&sig_act.sa_mask);
    int sigact_res = sigaction(SIGINT, &sig_act, NULL);
    if (sigact_res != 0) {
        perror("sigaction");
        cleanUp();
        exit(ERROR_SIG_HANDLER_INIT);
    }

    initRwLockAttr();
    initThreadAttr();
    initCacheList();

    int listen_fd = initListener(port);
    addFdToPollFds(&poll_fds, &poll_last_index, &POLL_TABLE_SIZE, listen_fd, POLLIN);


    while (!is_stop) {
        fprintf(stderr, "main: poll()\n");
        int poll_res = poll(poll_fds, poll_last_index, TIMEOUT * 1000);
        if (poll_res < 0) {
            perror("poll");
            break;
        }
        else if (poll_res == 0) {
            fprintf(stdout, "proxy timeout\n");
            break;
        }
        int num_handled_fd = 0;
        size_t i = 0;
        size_t prev_last_index = poll_last_index;
        fprintf(stderr, "main: poll_res = %d\n", poll_res);
        /*for (int j = 0; j < prev_last_index; j++) {
            fprintf(stderr, "poll_fds[%d] = %d : ", j, poll_fds[j].fd);
            if (poll_fds[j].revents & POLLIN) {
                fprintf(stderr, "POLLIN ");
            }
            if (poll_fds[j].revents & POLLOUT) {
                fprintf(stderr, "POLLOUT ");
            }
            fprintf(stderr, "\n");
        }*/
        while (num_handled_fd < poll_res && i < prev_last_index && !is_stop) {
            if (poll_fds[i].fd == READ_STOP_FD && (poll_fds[i].revents & POLLIN)) {
                removeFromPollFds(poll_fds, &poll_last_index, READ_STOP_FD);
                READ_STOP_FD = -1;
                destroyPollFds(poll_fds, &poll_last_index);
                cleanUp();
                pthread_exit(0);
            }
            if (poll_fds[i].fd == listen_fd && (poll_fds[i].revents & POLLIN)) {
                acceptNewClient(listen_fd);
                num_handled_fd += 1;
            }
            i += 1;
        }

        //printCacheList();
    }
    removeFromPollFds(poll_fds, &poll_last_index, READ_STOP_FD);
    READ_STOP_FD = -1;
    destroyPollFds(poll_fds, &poll_last_index);
    cleanUp();
    pthread_exit(0);
}

