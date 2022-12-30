#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <netinet/tcp.h>
#include <sys/types.h>
#include <netdb.h>
#include <poll.h>
#include <stdbool.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>

#define POLL_TIMEOUT (10000)
#define EMPTY (-1)
#define URL_SIZE (1024)
#define SERVICE_FDS_AMOUNT (2)
#define SUB_SIZE (8)

#define HTTP_DELIM "://"
#define GET_PREFIX "GET "
#define DEFAULT_PORT (80)

#define NO_HOST (-1)
#define NO_CLIENT (-1)

size_t CACHE_SIZE = 16;
size_t CURRENT_CLIENTS_AMOUNT = 8;
size_t CURRENT_HOSTS_AMOUNT = 8;
size_t SERVER_FDS_AMOUNT = 16;
size_t TOTAL_FDS_AMOUNT = 16 + SERVICE_FDS_AMOUNT;
size_t CURRENT_POLL_FDS = 0;
size_t MAX_CACHE = 0;
size_t CURRENT_CACHE_SIZE = 0;

int stop_fd = -1;

typedef struct Client {
    int fd;
    char *request;
    size_t request_size;

    int cache_idx;
    int last_written_idx;
} Client;

typedef struct Host {
    int fd;
    int cache_idx;
    int last_written_idx;
    bool header_parsed;
} Host;

typedef struct Cache {
    size_t request_hash;
    size_t host_idx;

    int *subscribers;
    size_t subs_size;
    size_t curr_subs;

    char *request;
    size_t request_size;

    char *response;
    size_t response_size;
    size_t response_idx;

    time_t full_time;

    bool full;
    bool valid;
    bool successful_request;
} Cache;

struct Cache *cache = NULL;
struct pollfd *server_fds = NULL;
struct Client *clients = NULL;
struct Host *hosts = NULL;

size_t hash(char *str){
    if (str == NULL) {
        return -1;
    }

    size_t hash = 5381;
    char tmp[URL_SIZE];
    memcpy(tmp, str, strlen(str) + 1);
    int c;

    for (int i = 0; tmp[i] != '\0'; ++i) {
        c = tmp[i];
        hash = ((hash << 5) + hash) + c;
    }

    return hash;
}

int ParseInt(char* str) {
    char *endpoint;
    int res = (int)strtol(str, &endpoint, 10);

    if (*endpoint != '\0') {
        fprintf(stderr, "Unable to parse: '%s' as int\n", str);
        res = -1;
    }
    return res;
}

void UpdatePollFdsAmount(int deleted_idx) {
    if (deleted_idx == CURRENT_POLL_FDS) {
        CURRENT_POLL_FDS--;
    }

    for (int i = CURRENT_POLL_FDS - 1; i > 0; --i) {
        if (server_fds[i].fd == EMPTY) {
            CURRENT_POLL_FDS--;
        } else {
            break;
        }
    }
}

size_t GetResponseLenght(char* header) {
    int len_str_size = strlen("Content-Length: ");
    char* len_start = strstr(header, "Content-Length: ");
    char* len_end = strstr(len_start, "\r\n");
    char tmp[URL_SIZE];
    memcpy(tmp, len_start + len_str_size, len_end - len_start - len_str_size);
    tmp[len_end - len_start - len_str_size] = '\0';
    int lenght = ParseInt(tmp);
    return lenght;
}

void DeleteFromPoll(int fd) {
    if (fd < 0) {
        return;
    }
    int deleted_idx = 0;
    for (int i = 0; i < CURRENT_POLL_FDS; ++i) {
        if (server_fds[i].fd == fd) {
            close(server_fds[i].fd);
            server_fds[i].fd = EMPTY;
            server_fds[i].events = 0;
            server_fds[i].revents = 0;
            deleted_idx = i;
            break;
        }
    }
    UpdatePollFdsAmount(deleted_idx);
}

void FreeServerFds() {
    if (server_fds == NULL) {
        return;
    }

    for (int i = 0; i < CURRENT_POLL_FDS; ++i) {
        if (server_fds[i].fd > 0) {
            close(server_fds[i].fd);
        }
    }
    free(server_fds);
}

void FreeClients() {
    if (clients == NULL) {
        return;
    }

    for (int i = 0; i < CURRENT_CLIENTS_AMOUNT; ++i) {
        free(clients[i].request);
        DeleteFromPoll(clients[i].fd);
        close(clients[i].fd);
    }
    free(clients);
}

void FreeHosts() {
    if (hosts == NULL) {
        return;
    }

    for (int i = 0; i < CURRENT_HOSTS_AMOUNT; ++i) {
        DeleteFromPoll(hosts[i].fd);
        close(hosts[i].fd);
    }
    free(hosts);
}

void FreeCache() {
    if (cache == NULL) {
        return;
    }

    for (int i = 0; i < CACHE_SIZE; ++i) {
        free(cache[i].subscribers);
        free(cache[i].response);
        free(cache[i].request);
    }
    free(cache);
}

void CleanUp() {
    FreeClients();
    FreeHosts();
    FreeCache();
    FreeServerFds();
}

void InitHost(int i) {
    hosts[i].fd = EMPTY;
    hosts[i].cache_idx = -1;
    hosts[i].last_written_idx = -1;
    hosts[i].header_parsed = false;
}

void InitHosts() {
    hosts = (Host*)malloc(CURRENT_HOSTS_AMOUNT * sizeof(Host));
    if (hosts == NULL) {
        fprintf(stderr, "Error in allocating memory for hosts!\n");
        CleanUp();
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < CURRENT_HOSTS_AMOUNT; ++i) {
        InitHost(i);
    }
}

void ReallocHosts() {
    size_t prev_size = CURRENT_HOSTS_AMOUNT;
    CURRENT_HOSTS_AMOUNT *= 2;
    hosts = realloc(hosts, CURRENT_HOSTS_AMOUNT * sizeof(Host));
    if (hosts == NULL) {
        fprintf(stderr, "Error in reallocating memory for hosts!\n");
        CleanUp();
        exit(EXIT_FAILURE);
    }

    for (int i = prev_size; i < CURRENT_HOSTS_AMOUNT; ++i) {
        InitHost(i);
    }
}

int GetHostId(int fd) {
    if (fd < 0) {
        return NO_HOST;
    }

    for (int i = 0; i < CURRENT_HOSTS_AMOUNT; ++i) {
        if (hosts[i].fd == fd) {
            return i;
        }
    }
    return NO_HOST;
}

int SetForFreeHostId(int fd) {
    if (fd < 0) {
        return -1;
    }

    for (int i = 0; i < CURRENT_HOSTS_AMOUNT; ++i) {
        if (hosts[i].fd == -1) {
            hosts[i].fd = fd;
            return i;
        }
    }

    int prev_size = (int)CURRENT_HOSTS_AMOUNT;
    ReallocHosts();
    hosts[prev_size].fd = fd;
    return prev_size;
}

void InitClient(int i) {
    clients[i].fd = EMPTY;
    clients[i].cache_idx = -1;
    clients[i].last_written_idx = -1;
    clients[i].request = NULL;
    clients[i].request_size = 0;
}

void InitClients() {
    clients = (Client*)malloc(CURRENT_CLIENTS_AMOUNT * sizeof(Client));
    if (clients == NULL) {
        fprintf(stderr, "Error in allocating memory for clients!\n");
        CleanUp();
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < CURRENT_CLIENTS_AMOUNT; ++i) {
        InitClient(i);
    }
}

void ReallocClients() {
    size_t prev_size = CURRENT_CLIENTS_AMOUNT;
    CURRENT_CLIENTS_AMOUNT *= 2;
    clients = realloc(clients, CURRENT_CLIENTS_AMOUNT * sizeof(Client));
    if (clients == NULL) {
        fprintf(stderr, "Error in reallocating memory for clients!\n");
        CleanUp();
        exit(EXIT_FAILURE);
    }

    for (int i = prev_size; i < CURRENT_CLIENTS_AMOUNT; ++i) {
        InitClient(i);
    }
}

int GetClientId(int fd) {
    if (fd < 0) {
        return NO_CLIENT;
    }

    for (int i = 0; i < CURRENT_CLIENTS_AMOUNT; ++i) {
        if (clients[i].fd == fd) {
            return i;
        }
    }
    return NO_CLIENT;
}

int SetForFreeClientId(int client_fd) {
    if (client_fd < 0) {
        return -1;
    }

    for (int i = 0; i < CURRENT_CLIENTS_AMOUNT; ++i) {
        if (clients[i].fd == EMPTY) {
            clients[i].fd = client_fd;
            return i;
        }
    }
    
    size_t prev_size = CURRENT_CLIENTS_AMOUNT;
    ReallocClients();
    clients[prev_size].fd = client_fd;
    return (int)prev_size;
}

void DeleteSub(int client_idx, int cache_idx) {
    if (cache_idx < 0 || cache_idx > CACHE_SIZE || 
        client_idx < 0 || client_idx > CURRENT_CLIENTS_AMOUNT) {
        return;
    }

    int subs_size = cache[cache_idx].subs_size;
    for (int i = 0; i < subs_size; ++i) {
        if (cache[cache_idx].subscribers[i] == client_idx) {
            cache[cache_idx].subscribers[i] = -1;
            cache[cache_idx].curr_subs--;
        }
    }
}

void DisconnectClient(int idx) {
    if (idx < 0 || idx > CURRENT_CLIENTS_AMOUNT) {
        return;
    }

    if (clients[idx].request != NULL) {
        free(clients[idx].request);
        clients[idx].request = NULL;
        clients[idx].request_size = 0;
        clients[idx].last_written_idx = 0;
    }

    if (clients[idx].fd != EMPTY) {
        DeleteFromPoll(clients[idx].fd);
        clients[idx].fd = EMPTY;
    }

    if (clients[idx].cache_idx != -1) {
        DeleteSub(idx, clients[idx].cache_idx);
        clients[idx].cache_idx = -1;
        clients[idx].last_written_idx = -1;
    }
}

void InitCacheItem(int i) {
    if (i >= CACHE_SIZE) {
        return;
    }

    cache[i].response = NULL;
    cache[i].request = NULL;
    cache[i].subscribers = NULL;
    cache[i].response_idx = 0;
    cache[i].response_size = 0;
    cache[i].request_size = 0;
    cache[i].subs_size = 0;
    cache[i].curr_subs = 0;
    cache[i].request_hash = -1;
    cache[i].host_idx = -1;
    cache[i].full_time = -1;
    cache[i].full = false;
    cache[i].valid = false;
    cache[i].successful_request = false;
}

void InitCache() {
    cache = (Cache*)malloc(CACHE_SIZE * sizeof(Cache));
    if (cache == NULL) {
        fprintf(stderr, "Error in allocating memory for cache!\n");
        CleanUp();
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < CACHE_SIZE; ++i) {
        InitCacheItem(i);
    }
}

void ReallocCache() {
    size_t prev_size = CACHE_SIZE;
    CACHE_SIZE *= 2;
    cache = realloc(cache, CACHE_SIZE * sizeof(Cache));
    if (cache == NULL) {
        fprintf(stderr, "Error in reallocating memory for cache!\n");
        CleanUp();
        exit(EXIT_FAILURE);
    }

    for (size_t i = prev_size; i < CACHE_SIZE; ++i) {
        InitCacheItem(i);
    }
}

int TryToFindAtCache(char* request) {
    size_t request_hash = hash(request);
    for (int i = 0; i < CACHE_SIZE; ++i) {
        if (cache[i].valid && cache[i].request_hash == request_hash) {
            cache[i].full_time = time(NULL);
            return i;
        }
    }
    return -1;
}

int AddToCache(char* request) {
    size_t request_hash = hash(request);
    int first_free_item = -1;
    for (int i = 0; i < CACHE_SIZE; ++i) {
        if (!cache[i].valid) {
            first_free_item = i;
            break;
        }
    }

    if (first_free_item == -1) {
        first_free_item = CACHE_SIZE;
        ReallocCache();
    }

    cache[first_free_item].request_hash = request_hash;
    cache[first_free_item].valid = true;

    int request_len = strlen(request);
    if (cache[first_free_item].request == NULL) {
        cache[first_free_item].request_size = request_len;
        cache[first_free_item].request = (char*)malloc(request_len);
        if (cache[first_free_item].request == NULL) {
            cache[first_free_item].valid = false;
            return -1;
        }
        memcpy(cache[first_free_item].request, request, request_len);
    }
    return first_free_item;
}

int GetFreeCacheItemId() {
    for (int i = 0; i < CACHE_SIZE; ++i) {
        if (!cache[i].valid) {
            return i;
        }
    }

    int prev_size = CACHE_SIZE;
    ReallocCache();
    return (int)prev_size;
}

void InitServerFd(int i) {
    server_fds[i].fd = EMPTY;
    server_fds[i].events = 0;
    server_fds[i].revents = 0;
}

void InitServerFds() {
    server_fds = (struct pollfd*)malloc(TOTAL_FDS_AMOUNT * sizeof(struct pollfd));
    if (server_fds == NULL) {
        fprintf(stderr, "Error in allocating memory for server_fds!\n");
        CleanUp();
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < TOTAL_FDS_AMOUNT; ++i) {
        InitServerFd(i);
    }
    CURRENT_POLL_FDS = 0;
}

void ReallocServerFds() {
    SERVER_FDS_AMOUNT *= 2;
    TOTAL_FDS_AMOUNT = SERVER_FDS_AMOUNT + SERVICE_FDS_AMOUNT;
    server_fds = realloc(server_fds, TOTAL_FDS_AMOUNT * sizeof(struct pollfd));
    if (server_fds == NULL) {
        fprintf(stderr, "Error in reallocating memory for server_fds!\n");
        CleanUp();
        exit(EXIT_FAILURE);
    }

    for (int i = CURRENT_POLL_FDS; i < TOTAL_FDS_AMOUNT; ++i) {
        InitServerFd(i);
    }
}

void AddFdTOServerFds(int fd, int events) {
    if (CURRENT_POLL_FDS < TOTAL_FDS_AMOUNT) {
        for (int i = 0; i < CURRENT_POLL_FDS; ++i) {
            if (server_fds[i].fd == EMPTY) {
                server_fds[i].fd = fd;
                server_fds[i].events = events;
                server_fds[i].revents = 0;
                return;
            }
        }
    }

    if (CURRENT_POLL_FDS >= TOTAL_FDS_AMOUNT) {
        ReallocServerFds();
    }

    server_fds[CURRENT_POLL_FDS].fd = fd;
    server_fds[CURRENT_POLL_FDS].events = events;
    server_fds[CURRENT_POLL_FDS].revents = 0;
    CURRENT_POLL_FDS++;
}

void SetEventForFd(int fd, int events) {
    for (int i = 0; i < CURRENT_POLL_FDS; ++i) {
        if (server_fds[i].fd == fd) {
            server_fds[i].events = events;
            server_fds[i].revents = 0;
        }
    }
}

void UpdateRevents() {
    for (int i = 0; i < TOTAL_FDS_AMOUNT; ++i) {
        server_fds[i].revents = 0;
    }
}

int ReallocSubs(int cache_idx) {
    int prev_size = cache[cache_idx].subs_size;
    cache[cache_idx].subs_size *= 2;
    int subs_size = cache[cache_idx].subs_size;
    cache[cache_idx].subscribers = realloc(cache[cache_idx].subscribers, subs_size * sizeof(int));
    if (cache[cache_idx].subscribers == NULL) {
        fprintf(stderr, "Error in reallocating memory for cache[%d] subscribers!\n", cache_idx);
        cache[cache_idx].subs_size = prev_size;
        return -1;
    }


    memset(cache[cache_idx].subscribers + prev_size, -1, (subs_size - prev_size) * sizeof(int));
    return 0;
}

void AddSub(int client_idx, int cache_idx) {
    if (cache_idx < 0 || cache_idx > CACHE_SIZE || 
        client_idx < 0 || client_idx > CURRENT_CLIENTS_AMOUNT ||  
        !cache[cache_idx].valid) {
        return;
    }

    cache[cache_idx].curr_subs++;
    if (cache[cache_idx].subs_size <= 0) {
        cache[cache_idx].subs_size = SUB_SIZE;
        cache[cache_idx].subscribers = (int*)malloc(SUB_SIZE * sizeof(int));
        if (cache[cache_idx].subscribers == NULL) {
            DisconnectClient(client_idx);
        }

        memset(cache[cache_idx].subscribers, -1, SUB_SIZE * sizeof(int));
    }

    if (cache[cache_idx].curr_subs >= cache[cache_idx].subs_size) {
        if (ReallocSubs(cache_idx) == -1) {
            DisconnectClient(client_idx);
        }
    }

    for (int i = 0; i < cache[cache_idx].subs_size; ++i) {
        if (cache[cache_idx].subscribers[i] == client_idx || cache[cache_idx].subscribers[i] == -1) {
            if (cache[cache_idx].subscribers[i] == -1) {
                cache[cache_idx].curr_subs++;
            }
            cache[cache_idx].subscribers[i] = client_idx;
            return;
        }
    }
}

void NotifySub(int cache_idx, int events) {
    int subs_size = cache[cache_idx].subs_size;
    for (int i = 0; i < subs_size; ++i) {
        int client_idx = cache[cache_idx].subscribers[i];
        if (client_idx != EMPTY) {
            SetEventForFd(clients[client_idx].fd, events);
            if (clients[client_idx].last_written_idx < 0) {
                clients[client_idx].last_written_idx = 0;
            }
        }
    }
}

void DisconnectHost(int host_idx) {
    if (host_idx < 0 || host_idx > CURRENT_HOSTS_AMOUNT) {
        return;
    }

    hosts[host_idx].last_written_idx = -1;
    hosts[host_idx].header_parsed = false;
    int cache_idx = hosts[host_idx].cache_idx;
    if (cache_idx >= 0) {
        cache[cache_idx].host_idx = -1;
        NotifySub(cache_idx, POLLIN | POLLOUT);
        hosts[host_idx].cache_idx = -1;
    }

    int host_fd = hosts[host_idx].fd;
    if (host_fd != EMPTY) {
        DeleteFromPoll(host_fd);
        hosts[host_idx].fd = EMPTY;
    }
}

int GetOldestRecord() {
    time_t max_timeout = time(NULL);
    int res = -1;
    for (int i = 0; i < CACHE_SIZE; ++i) {
        if (cache[i].full_time < max_timeout && cache[i].full_time != -1) {
            max_timeout = cache[i].full_time;
            res = i;
        }
    }
    return res;
}

void CleanCacheItem(int cache_idx) {
    if (cache_idx < 0 || cache_idx > CACHE_SIZE) {
        return;
    }

    cache[cache_idx].full = false;
    cache[cache_idx].full_time = -1;
    cache[cache_idx].valid = false;
    cache[cache_idx].successful_request = false;
    cache[cache_idx].request_hash = -1;

    if (cache[cache_idx].request != NULL) {
        free(cache[cache_idx].request);
        cache[cache_idx].request = NULL;
    }
    cache[cache_idx].request_size = 0;
    
    if (cache[cache_idx].response != NULL) {
        free(cache[cache_idx].response);
        cache[cache_idx].response = NULL;
    }
    cache[cache_idx].response_size = 0;
    cache[cache_idx].response_idx = 0;
    
    if (cache[cache_idx].host_idx != EMPTY) {
        DisconnectHost(cache[cache_idx].host_idx);
        cache[cache_idx].host_idx = EMPTY;
    }

    if (cache[cache_idx].subscribers != NULL) {
        int subs_size = cache[cache_idx].subs_size;
        cache[cache_idx].subs_size = 0;
        for (int i = 0; i < subs_size; ++i) {
            if (cache[cache_idx].subscribers[i] != EMPTY) {
                DisconnectClient(cache[cache_idx].subscribers[i]);
            }
        }
        free(cache[cache_idx].subscribers);
        cache[cache_idx].subscribers = NULL;
        cache[cache_idx].curr_subs = 0;
    }
}

int FreeMemoryForNewCacheRecord(int needed_bytes) {
    int oldest_record = GetOldestRecord();
    while (MAX_CACHE - CURRENT_CACHE_SIZE < needed_bytes && oldest_record != -1) {
        CURRENT_CACHE_SIZE -= cache[oldest_record].response_size;
        CleanCacheItem(oldest_record);
        oldest_record = GetOldestRecord();
    }

    if (MAX_CACHE - CURRENT_CACHE_SIZE > needed_bytes) {
        return 0;
    }
    return -1;
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
    *port = DEFAULT_PORT;
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

int GetResponseStatus(char *header) {
    int first_str_end = strstr(header, "\n") - header;
    char *first_str = (char*)malloc(first_str_end);
    memcpy(first_str, header, first_str_end);
    char *status_str = strstr(first_str, " ");
    status_str++;
    char *tmp = strstr(status_str, " ");
    int status_end = tmp - status_str;
    status_str[status_end] = '\0';
    int status = ParseInt(status_str);
    free(first_str);
    return status;
}

int ConnectToHost(char* host, int port) {
    if (host == NULL || port < 0 || port > 65535) {
        return -1;
    }

    struct hostent *hp = gethostbyname(host);
    if (hp == NULL) {
        fprintf(stderr, "Unable to find host: %s! Error: %s\n", host, strerror(h_errno));
        return -1;
    }
    struct sockaddr_in addr;
    memcpy(&addr.sin_addr, hp->h_addr, hp->h_length);
    addr.sin_port = htons(port);
    addr.sin_family = AF_INET;

    int host_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (host_socket == -1) {
        fprintf(stderr, "Unable to create socket! Error: %s\n", strerror(errno));
        return -1;
    }

    int opt_val = 1;
    setsockopt(host_socket, IPPROTO_TCP, TCP_NODELAY, (const char *)&opt_val, sizeof(int));

    int connection = connect(host_socket, (struct sockaddr *)&addr, sizeof(struct sockaddr_in));
    if (connection == -1) {
        fprintf(stderr, "Unable to connect to host: %s! Error: %s\n", host, strerror(errno));
        return -1;
    }
    return host_socket;

}

void ReadFromClient(int idx) {
    if (idx < 0 || idx > CURRENT_CLIENTS_AMOUNT) {
        return;
    }

    char buf[URL_SIZE];
    ssize_t readed_bytes = read(clients[idx].fd, buf, URL_SIZE);
    if (readed_bytes < 0) {
        fprintf(stderr, "Error in read from %d client: %s\n", idx, strerror(errno));
        DisconnectClient(idx);
        return;
    }

    if (readed_bytes == 0) {
        // printf("Close connection for %d client\n", idx);
        DisconnectClient(idx);
        return;
    }
    buf[readed_bytes] = '\0';
    if (clients[idx].request_size == 0) {
        clients[idx].request_size = URL_SIZE;
        clients[idx].request = (char*)malloc(URL_SIZE);
        if (clients[idx].request == NULL) {
            fprintf(stderr, "Error in allocating memory for %d client request!\n", idx);
            DisconnectClient(idx);
            return;
        }
    }
    memcpy(clients[idx].request, buf, readed_bytes);

    char url[URL_SIZE];
    int err = ExtractUrl(url, buf);
    if (err != 0) {
        fprintf(stderr, "Unsupported method\n");
        DisconnectClient(idx);
        return;
    }

    int cache_idx = TryToFindAtCache(buf);
    if (cache_idx != -1) {
        AddSub(idx, cache_idx);
        clients[idx].cache_idx = cache_idx;
        clients[idx].last_written_idx = 0;
        if (cache[cache_idx].response_idx != 0) {
            SetEventForFd(clients[idx].fd, POLLIN | POLLOUT);
        }
        return;
    }

    cache_idx = AddToCache(buf);
    if (cache_idx == -1) {
        DisconnectClient(idx);
        return;
    }

    char host[URL_SIZE];
    char path[URL_SIZE];
    int port;
    ParseFullURL(host, path, &port, url);
    printf("Parsed url: host=%s, path=%s, port=%d\n", host, path, port);

    int host_fd = ConnectToHost(host, port);
    if (host_fd == -1) {
        DisconnectClient(idx);
        CleanCacheItem(cache_idx);
        return;
    }

    fcntl(host_fd, F_SETFL, O_NONBLOCK);

    int host_idx = SetForFreeHostId(host_fd);
    hosts[host_idx].cache_idx = cache_idx;
    hosts[host_idx].last_written_idx = 0;

    cache[cache_idx].host_idx = host_idx;
    AddSub(idx, cache_idx);

    clients[idx].cache_idx = cache_idx;
    clients[idx].last_written_idx = 0;
    AddFdTOServerFds(host_fd, POLLIN | POLLOUT);
}

void WriteToClient(int idx) {
    if (idx < 0 || idx > CURRENT_CLIENTS_AMOUNT) {
        return;
    }

    int cache_idx = clients[idx].cache_idx;
    if (cache[cache_idx].host_idx == EMPTY && !cache[cache_idx].valid) {
        CleanCacheItem(cache_idx);
        return;
    }

    int client_fd = clients[idx].fd;
    int client_last_written_idx = clients[idx].last_written_idx;
    int response_idx = cache[cache_idx].response_idx;
    int count = response_idx - client_last_written_idx;
    ssize_t written_bytes = write(client_fd, &cache[cache_idx].response[client_last_written_idx], count);

    if (written_bytes == -1) {
        fprintf(stderr, "Error while write to %d client! Error: %s\n", idx, strerror(errno));
        DisconnectClient(idx);
        return;
    }

    clients[idx].last_written_idx += (int)written_bytes;
    if (clients[idx].last_written_idx == response_idx) {
        SetEventForFd(client_fd, POLLIN);
    }

    if (!cache[cache_idx].successful_request && cache[cache_idx].full) {
        CleanCacheItem(cache_idx);
        clients[idx].cache_idx = -1;
    }
}

void ReadFromHost(int idx) {
    if (idx < 0 || idx > CURRENT_HOSTS_AMOUNT) {
        return;
    }

    int cache_idx = hosts[idx].cache_idx;
    char buf[BUFSIZ];
    ssize_t readed_bytes = read(hosts[idx].fd, buf, BUFSIZ);

    if (readed_bytes < 0) {
        fprintf(stderr, "Error while read from %d host! Error: %s\n", idx, strerror(errno));
        return;
    }

    if (readed_bytes == 0) {
        cache[cache_idx].full = true;
        cache[cache_idx].full_time = time(NULL);
        cache[cache_idx].response_size = cache[cache_idx].response_idx;
        NotifySub(cache_idx, POLLIN | POLLOUT);
        DisconnectHost(idx);
        return;
    }

    if (cache[cache_idx].response_size == 0) {
        size_t content_len = GetResponseLenght(buf);
        if (content_len == -1) {
            fprintf(stderr, "Error header!\n");
            DisconnectHost(idx);
            return;
        }
        if (content_len > MAX_CACHE) {
            printf("to big\n");
            DisconnectHost(idx);
            return;
            //readThrow todo
        }
        if (content_len > MAX_CACHE - CURRENT_CACHE_SIZE) {
            printf("here\n");
            int err = FreeMemoryForNewCacheRecord(content_len);
            if (err != 0) {
                fprintf(stderr, "Error in allocating memory for %d host response!\n", idx);
                DisconnectHost(idx);
                return;
            }
        }
        CURRENT_CACHE_SIZE += content_len;
        cache[cache_idx].response_size = BUFSIZ;
        cache[cache_idx].response = (char*)malloc(BUFSIZ);
        if (cache[cache_idx].response == NULL) {
            fprintf(stderr, "Error in allocating memory for %d host response!\n", idx);
            DisconnectHost(idx);
            return;
        }
    }

    int response_idx = cache[cache_idx].response_idx;
    int response_size = cache[cache_idx].response_size;
    if (readed_bytes + response_idx >= response_size) {
        cache[cache_idx].response_size *= 2;
        cache[cache_idx].response = realloc(cache[cache_idx].response, response_size * 2);
        if (cache[cache_idx].response == NULL) {
            fprintf(stderr, "Error in reallocating memory for %d host response!\n", idx);
            DisconnectHost(idx);
            return;
        }
    }
    memcpy(&cache[cache_idx].response[response_idx], buf, readed_bytes);
    cache[cache_idx].response_idx += readed_bytes;
    NotifySub(cache_idx, POLLIN | POLLOUT);
    if (!hosts[idx].header_parsed) {
        hosts[idx].header_parsed = true;
        int status = GetResponseStatus(buf);
        if (status >= 200 && status < 300) {
            cache[cache_idx].successful_request = true;
        }
    }
}

void WriteToHost(int idx) {
    if (idx < 0 || idx > CURRENT_HOSTS_AMOUNT) {
        return;
    }

    int host_fd = hosts[idx].fd;
    int cache_idx = hosts[idx].cache_idx;
    int last_written_idx = hosts[idx].last_written_idx;
    int request_size = cache[cache_idx].request_size;
    ssize_t written_bytes = write(host_fd, &cache[cache_idx].request[last_written_idx], request_size - last_written_idx);

    if (written_bytes == -1) {
        fprintf(stderr, "Error while write to %d host! Error: %s\n", idx, strerror(errno));
        DisconnectHost(idx);
        return;
    }

    hosts[idx].last_written_idx += written_bytes;
    if (hosts[idx].last_written_idx == request_size) {
        SetEventForFd(host_fd, POLLIN);
    }
}

void SigHandler(int signal) {
    if (signal == SIGINT && stop_fd != -1) {
        write(stop_fd, "c", 1);
        close(stop_fd);
        stop_fd = -1;
    }
}

void AcceptClient(int listen_fd) {
    int new_fd = accept(listen_fd, NULL, NULL);
    if (new_fd == -1) {
        perror("Error while accept client");
        return;
    }

    fcntl(new_fd, F_SETFL, O_NONBLOCK);

    int idx = SetForFreeClientId(new_fd);
    AddFdTOServerFds(new_fd, POLLIN);
    printf("accepted new client: %d\n", idx);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Bad number of args!\nUsage: %s <port> <max cache in Mb>\n", argv[0]);
        return 1;
    }

    int server_port = ParseInt(argv[1]);
    if (server_port == -1 || server_port > 65535) {
        fprintf(stderr, "Invalid port number!\n");
        return 1;
    }

    MAX_CACHE = ParseInt(argv[2]) * 1024 * 1024;

    InitServerFds();

    int pipe_fds[2];
    if (pipe(pipe_fds) != 0) {
        perror("Error in PIPE(2)");
        FreeServerFds();
        return 1;
    }

    AddFdTOServerFds(pipe_fds[0], POLLIN);
    stop_fd = pipe_fds[1];

    struct sigaction sa;
    sa.sa_handler = SigHandler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(struct sockaddr_in));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(server_port);

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1) {
        perror("Error in SOCKET(2)");
        FreeServerFds();
        return 1;
    }

    if (bind(listen_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) != 0) {
        perror("Error in BIND(2)");
        FreeServerFds();
        close(listen_fd);
    }

    if (listen(listen_fd, CURRENT_CLIENTS_AMOUNT) != 0) {
        perror("Error in LISTEN(2)");
        FreeServerFds();
        close(listen_fd);
    }

    AddFdTOServerFds(listen_fd, POLLIN);

    InitClients();
    InitHosts();
    InitCache();

    while (true) {
        UpdateRevents();
        int res = poll(server_fds, CURRENT_POLL_FDS, POLL_TIMEOUT);

        if (res < 0 && errno != EINTR) {
            perror("Error in POLL(2)");
            break;
        }
        size_t last_fd = CURRENT_POLL_FDS;

        int handled_fd = 0;
        for (int i = 0; i < last_fd && handled_fd < res; ++i) {
            int curr_fd = server_fds[i].fd;
            if (curr_fd == pipe_fds[0] && (server_fds[i].revents & POLLIN)) {
                CleanUp();
                return 0;
            }

            if (curr_fd == listen_fd && (server_fds[i].revents & POLLIN)) {
                AcceptClient(listen_fd);
                handled_fd++;
                continue;
            }

            int client_idx = GetClientId(curr_fd);
            int host_idx = GetHostId(curr_fd);

            bool handled = false;
            if (server_fds[i].revents & POLLIN) {
                if (client_idx != NO_CLIENT) {
                    ReadFromClient(client_idx);
                } else if (host_idx != NO_HOST) {
                    ReadFromHost(host_idx);
                }
                handled = true;
            }

            if (server_fds[i].revents & POLLOUT) {
                if (client_idx != NO_CLIENT) {
                    WriteToClient(client_idx);
                } else if (host_idx != NO_HOST) {
                    WriteToHost(host_idx);
                }
                handled = true;
            }

            if (curr_fd >= 0 && client_idx == NO_CLIENT && host_idx == NO_HOST &&
                curr_fd != pipe_fds[0] && curr_fd != listen_fd) {
                DeleteFromPoll(curr_fd);
            }

            if (handled) {
                handled_fd++;
            }
        }
    }
    CleanUp();
    close(listen_fd);
    close(pipe_fds[0]);
    return 0;
}