#include "cache.h"
#include <malloc.h>
#include <string.h>
#include <unistd.h>

cache_list_t *initList() {
    cache_list_t *list = (cache_list_t *) calloc(1, sizeof(cache_list_t)); // Thread-safety
    if (list == NULL) {
        perror("Error: calloc returned NULL");
        return NULL;
    }
    pthread_mutex_init(&list->mutex, NULL);
    list->head = NULL;
    return list;
}

void destroyList(cache_list_t *list) {
    if (list == NULL) {
        return;
    }
    pthread_mutex_destroy(&list->mutex);
    while (list->head != NULL) {
        cache_node_t *tmp = list->head->next;
        freeCacheRecord(list->head->record);
        free(list->head->record);
        free(list->head);
        list->head = tmp;
    }
    free(list);
}

void freeCacheRecord(cache_t *record) {
    if (record == NULL) {
        return;
    }
    record->valid = false;
    record->private = true;
    if (record->url != NULL) {
        free(record->url);
        record->url = NULL;
        record->URL_LEN = 0;
    }
    if (record->request != NULL) {
        free(record->request);
        record->request = NULL;
        record->REQUEST_SIZE = 0;
    }
    if (record->response != NULL) {
        free(record->response);
        record->response = NULL;
    }
    record->response_index = 0;
    record->RESPONSE_SIZE = 0;
    pthread_mutex_lock(&record->subs_mutex);
    if (record->num_subscribers > 0) {
        record->num_subscribers = 0;
    }
    pthread_mutex_unlock(&record->subs_mutex);
    if (record->valid_subs_mutex) {
        pthread_mutex_destroy(&record->subs_mutex);
        record->valid_subs_mutex = false;
    }
    if (record->valid_rw_lock) {
        pthread_rwlock_destroy(&record->rw_lock);
        record->valid_rw_lock = false;
    }
    close(record->event_fd);
}

