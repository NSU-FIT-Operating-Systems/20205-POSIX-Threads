#include <sys/socket.h>
#include <netinet/in.h>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <cstring>
#include <sys/types.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <fcntl.h>
#include <pthread.h>
#include <csignal>
#include <poll.h>
#include <cerrno>
#include <vector>
#include <map>
#include <string>


#define CLIENT_QUEUE_SIZE (30)
#define POLL_TIMEOUT (1000)
#define POLL_HOST_TIMEOUT (1000)
#define URL_BUFF_SIZE (1024)
#define EXP_GROW_UP_LIMIT (500000000)
#define HTTP_DELIM "://"

#define CACHE_ITEM_STATUS_INIT (0)
#define CACHE_ITEM_STATUS_IN_PROCESS (1)
#define CACHE_ITEM_STATUS_FINISH (2)
#define CACHE_ITEM_STATUS_ERROR (3)


// Errors
#define UNABLE_TO_FIND_HOST (-1)
#define UNABLE_TO_CREATE_SOCKET (-2)
#define UNABLE_TO_CONNECT (-3)


typedef struct {
    bool exited;
    int fd;
    pthread_t thread;
} Client;

typedef struct {
    int status;
    char* content;
    size_t size;
    char* request;
    size_t request_size;
    pthread_rwlock_t rwlock;
    pthread_t thread;
    pthread_cond_t cv;
    pthread_mutex_t cvm;
} CacheItem;

// parsing constants
const std::string headers_delimiter = "\r\n";
const std::string host_header_str = "Host: ";

std::vector<Client*> clients;
std::map<std::string, CacheItem*> cache;

int running = 0;

pthread_mutex_t cache_mutex;

void SigHandler(int id) {
    if (id == SIGINT) {
        running = 0;
    }
}

int ParseInt(char* str) {
    char * endpoint;
    int res = (int)strtol(str, &endpoint, 10);

    if (*endpoint != '\0') {
        fprintf(stderr, "Unable to parse: '%s' as int\n", str);
        exit(1);
    }
    return res;
}

std::string ParseHeader(std::string request) {
    size_t pos = 0;
    std::string token;
    while ((pos = request.find(headers_delimiter)) != std::string::npos) {
        token = request.substr(0, pos);
        size_t found = token.find(host_header_str);
        if (found == 0) {
            token.erase(found, host_header_str.length());
            return token;
        }
        request.erase(0, pos + headers_delimiter.length());
    }
    return "";
}

void ParseFullURL(char* host, char* path, int* port, const char* full_url) {
    const char* url_without_http = strstr(full_url, HTTP_DELIM);
    if (url_without_http == nullptr) {
        url_without_http = full_url;
    } else {
        url_without_http += strlen(HTTP_DELIM);
    }
    const char* url_tmp = strpbrk(url_without_http, ":/");
    // default port
    *port = 80;
    if (url_tmp == nullptr) {
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

int ConnectToHost(char* host, int port) {
    struct hostent *hp = gethostbyname(host);
    if (hp == nullptr) {
        return UNABLE_TO_FIND_HOST;
    }
    struct sockaddr_in addr{};
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

void InitCacheItem(CacheItem* cache_item, char* request) {
    pthread_rwlock_init(&cache_item->rwlock, nullptr);
    cache_item->status = CACHE_ITEM_STATUS_INIT;
    cache_item->size = 0;
    cache_item->request = request;
    cache_item->request_size = strlen(request);
    pthread_cond_init(&cache_item->cv, nullptr);
    pthread_mutex_init(&cache_item->cvm, nullptr);
}

int WriteTo(int fd, const char* buff, size_t size) {
    size_t write_size = 0;
    while (write_size != size) {
        size_t write_iter = write(fd, buff + write_size, size - write_size);
        if (write_iter == -1) {
            return 1;
        }
        write_size += write_iter;
    }
    return 0;
}

void DisconnectClient(Client* client) {
    printf("Client disconnect\n");
    client->exited = true;
    close(client->fd);
    pthread_exit(nullptr);
}

void DisconnectReader(CacheItem* cache_item, int status) {
    cache_item->status = status;
    pthread_exit(nullptr);
}

void* Reader(void* arg) {
    printf("Start reading from host\n");
    CacheItem* cache_item = (CacheItem*)arg;

    std::string url = ParseHeader(cache_item->request);
    if (url.length() == 0) {
        fprintf(stderr, "Unable to extract url from request\n");
        DisconnectReader(cache_item, CACHE_ITEM_STATUS_ERROR);
    }
    char host[URL_BUFF_SIZE];
    char path[URL_BUFF_SIZE];
    int port;
    ParseFullURL(host, path, &port, url.c_str());
    printf("Parsed url: host=%s, path=%s, port=%d\n", host, path, port);
    int host_socket = ConnectToHost(host, port);
    bool connected = false;
    if (host_socket == UNABLE_TO_FIND_HOST) {
        fprintf(stderr, "Unable to find host: %s\n", host);
    } else if (host_socket == UNABLE_TO_CONNECT) {
        fprintf(stderr, "Unable to connect to host: %s\n", host);
    } else if (host_socket == UNABLE_TO_CREATE_SOCKET) {
        fprintf(stderr, "Unable to create socket\n");
    } else {
        connected = true;
        printf("Connected to %s (port %d)\n", host, port);
    }
    if (!connected) {
        DisconnectReader(cache_item, CACHE_ITEM_STATUS_ERROR);
    }

    int err = WriteTo(host_socket, cache_item->request, cache_item->request_size);
    if (err != 0) {
        fprintf(stderr, "Error while write to host\n");
        close(host_socket);
        DisconnectReader(cache_item, CACHE_ITEM_STATUS_ERROR);
    }

    int content_size = BUFSIZ;
    size_t read_count = 0;
    size_t read_iter = 1;
    cache_item->content = (char*)malloc(content_size * sizeof(char));
    if (cache_item->content == nullptr) {
        fprintf(stderr, "Unable to allocate memory for cache\n");
        close(host_socket);
        DisconnectReader(cache_item, CACHE_ITEM_STATUS_ERROR);
    }
    printf("Read from host\n");
    struct pollfd poll_fd{};
    poll_fd.fd = host_socket;
    poll_fd.events = POLLIN;

    while (running) {
        poll_fd.revents = 0;
        int poll_res = poll(&poll_fd, 1, POLL_HOST_TIMEOUT);

        if (poll_res == 0) {
            continue;
        }

        if (poll_res == -1 && errno != EINTR) {
            fprintf(stderr, "Error while poll\n");
            break;
        }

        if (poll_fd.revents & POLLIN) {
            if (content_size <= read_count) {
                pthread_rwlock_wrlock(&(cache_item->rwlock));
                int new_size = content_size + BUFSIZ;
                if (new_size <= EXP_GROW_UP_LIMIT) {
                    new_size = content_size * 2;
                }
                cache_item->content = (char*)realloc(cache_item->content, new_size * sizeof(char));
                content_size = new_size;
                pthread_rwlock_unlock(&(cache_item->rwlock));
            }
            read_iter = read(host_socket, cache_item->content + read_count, content_size - read_count);
            read_count += read_iter;

            // signal event -- new content
            pthread_mutex_lock(&cache_item->cvm);
            cache_item->size = read_count;
            pthread_cond_broadcast(&cache_item->cv);
            pthread_mutex_unlock(&cache_item->cvm);
            if (read_iter == 0) {
                close(host_socket);
                DisconnectReader(cache_item, CACHE_ITEM_STATUS_FINISH);
            }
        }
    }
    close(host_socket);
    pthread_mutex_lock(&cache_item->cvm);
    cache_item->status = CACHE_ITEM_STATUS_ERROR;
    pthread_cond_broadcast(&cache_item->cv);
    pthread_mutex_unlock(&cache_item->cvm);
    pthread_exit(nullptr);
}

void WaitContent(CacheItem* cache_item, size_t cur_size) {
    pthread_mutex_lock(&cache_item->cvm);
    while (cur_size >= cache_item->size && cache_item->status != CACHE_ITEM_STATUS_ERROR && cache_item->status != CACHE_ITEM_STATUS_FINISH)
        pthread_cond_wait(&cache_item->cv, &cache_item->cvm);
    pthread_mutex_unlock(&cache_item->cvm);
}

void* RunClient(void* arg) {
    printf("Handle client\n");
    Client* client = (Client*)arg;

    int client_fd = client->fd;

    struct pollfd poll_fd{};
    poll_fd.fd = client_fd;
    poll_fd.events = POLLIN;

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
            char* input_buffer = (char*)malloc(URL_BUFF_SIZE * sizeof(char));
            size_t read_bytes = read(client_fd, input_buffer, URL_BUFF_SIZE);
            if (read_bytes == -1) {
                fprintf(stderr, "Error while read from client\n");
                DisconnectClient(client);
            }
            input_buffer[read_bytes] = '\0';

            printf("Get request from client\n");
            std::string request(input_buffer);

            bool create_thread = false;

            pthread_mutex_lock(&cache_mutex);
            auto cache_item_iter = cache.find(request);
            CacheItem* cache_item;
            if (cache_item_iter != cache.end()) {
                cache_item = cache[request];
                free(input_buffer);
            } else {
                cache_item = (CacheItem*) malloc(sizeof (CacheItem));
                if (cache_item == nullptr) {
                    fprintf(stderr, "Unable to allocate memory for cache item. close client");
                    DisconnectClient(client);
                }
                InitCacheItem(cache_item, input_buffer);
                cache[request] = cache_item;
                create_thread = true;
            }
            pthread_mutex_unlock(&cache_mutex);

            if (create_thread) {
                cache_item->status = CACHE_ITEM_STATUS_IN_PROCESS;
                pthread_create(&cache_item->thread, nullptr, Reader, (void *)(cache_item));
                pthread_detach(cache_item->thread);
            }

            printf("Send response to client\n");
            size_t send_size = 0;
            while (true) {
                if (cache_item->status == CACHE_ITEM_STATUS_FINISH && cache_item->size == send_size) {
                    break;
                }
                if (cache_item->status == CACHE_ITEM_STATUS_ERROR) {
                    break;
                }
                // wait for new content
                if (cache_item->status != CACHE_ITEM_STATUS_FINISH) {
                    WaitContent(cache_item, send_size);
                }
                pthread_rwlock_rdlock(&cache_item->rwlock);
                size_t send_iter = write(client_fd, cache_item->content + send_size, cache_item->size - send_size);
                if (send_size == -1) {
                    fprintf(stderr, "Error while write to client\n");
                    break;
                }
                send_size += send_iter;
                pthread_rwlock_unlock(&cache_item->rwlock);
            }
            DisconnectClient(client);
        }
    }
    DisconnectClient(client);
    pthread_exit(nullptr);
}

void CheckClients() {
    for (auto it=clients.begin(); it != clients.end(); ) {
        if((*it)->exited) {
            it = clients.erase(it);
        } else {
            ++it;
        }
    }
}

int RunServer(int port) {

    // init address struct
    struct sockaddr_in addr{};
    memset(&addr, 0, sizeof(struct sockaddr_in));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    // init listen socket
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1){
        fprintf(stderr, "Error while create socket\n");
        return 1;
    }

    // bind socket to address
    int err = bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr));
    if (err != 0) {
        fprintf(stderr, "Error while bind socket\n");
        return 1;
    }

    err = listen(listen_fd, CLIENT_QUEUE_SIZE);
    if (err != 0) {
        fprintf(stderr, "Error while listen socket\n");
        return 1;
    }

    pthread_mutex_init(&cache_mutex, nullptr);

    struct pollfd poll_fd{};
    poll_fd.fd = listen_fd;
    poll_fd.events = POLLIN;

    running = 1;

    while (running) {
        poll_fd.revents = 0;
        int poll_res = poll(&poll_fd, 1, POLL_TIMEOUT);

        if (poll_res == 0) {
            CheckClients();
            continue;
        }

        if (poll_res == -1 && errno != EINTR) {
            fprintf(stderr, "Error while poll\n");
            break;
        }

        if (poll_fd.revents & POLLIN) {
            int new_client_fd = accept(listen_fd, nullptr, nullptr);
            if (new_client_fd == -1) {
                fprintf(stderr, "Error while accept client\n");
                continue;
            }

            Client* client = (Client*)malloc(sizeof(Client));
            client->exited = false;
            client->fd = new_client_fd;
            clients.push_back(client);
            pthread_create(
                    &client->thread,
                    nullptr,
                    RunClient,
                    (void *)(client)
            );
        }
    }
    running = 0;
    printf("\nGraceful shutdown:\n");

    printf("Joining clients threads...\n");
    for (const auto client : clients) {
        pthread_join(client->thread, nullptr);
    }

    printf("Free memory...\n");
    for (const auto client : clients) {
        free(client);
    }
    for (const auto& cache_item : cache) {
        pthread_rwlock_destroy(&cache_item.second->rwlock);
        pthread_mutex_destroy(&cache_item.second->cvm);
        pthread_cond_destroy(&cache_item.second->cv);
        free(cache_item.second->content);
        free(cache_item.second->request);
        free(cache_item.second);
    }

    printf("Close socket...\n");
    close(listen_fd);

    pthread_mutex_destroy(&cache_mutex);

    return 0;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr,
                "Specify exact 1 args: server port.\n");
        pthread_exit(nullptr);
    }
    // read params
    int port = ParseInt(argv[1]);

    //set interrupt handler
    struct sigaction sa{};
    sa.sa_handler = SigHandler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);

    // run server
    int err = RunServer(port);
    if (err != 0) {
        fprintf(stderr, "Error while run server. Free memory. Exiting\n");
        return 1;
    }

    return 0;
}