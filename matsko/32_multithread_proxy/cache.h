#ifndef INC_33_PROXY_WITH_THREAD_POOL_CACHE_H
#define INC_33_PROXY_WITH_THREAD_POOL_CACHE_H
#include <pthread.h>
#include <stdbool.h>

typedef struct cache {
    char *url;
    char *request;
    char *response;
    pthread_rwlock_t rw_lock;
    pthread_mutex_t subs_mutex;
    size_t URL_LEN;
    size_t REQUEST_SIZE;
    size_t RESPONSE_SIZE;
    size_t response_index;
    int num_subscribers;
    int event_fd;
    bool server_alive;
    bool full;
    bool valid;
    bool private;
    bool valid_rw_lock;
    bool valid_subs_mutex;
} cache_t;

typedef struct cache_node_t {
    cache_t *record;
    struct cache_node_t *next;
} cache_node_t;

typedef struct list_t {
    struct cache_node_t *head;
    pthread_mutex_t mutex;
} cache_list_t;

cache_list_t *initList();
void destroyList(cache_list_t *list);
void freeCacheRecord(cache_t *record);

#endif //INC_33_PROXY_WITH_THREAD_POOL_CACHE_H
