#include <stdlib.h>
#include "thread_safe_queue.c"
#include <semaphore.h>
#include <pthread.h>
#include <stdbool.h>
#include <string.h>

typedef struct MyQueue {
    Queue* queue;
    sem_t full_sem;
    sem_t empty_sem;
    pthread_mutex_t mutex;
    bool dropped;
} MyQueue;

void mymsginit(MyQueue* myQueue) {
    myQueue->queue = create();
    myQueue->dropped = false;
    sem_init(&myQueue->full_sem, 0, 10);
    sem_init(&myQueue->empty_sem, 0, 0);
    pthread_mutex_init(&myQueue->mutex, NULL);
}

void mymsqdrop(MyQueue* myQueue) {
    pthread_mutex_lock(&myQueue->mutex);
    myQueue->dropped = true;
    sem_post(&myQueue->empty_sem);
    sem_post(&myQueue->full_sem);
    pthread_mutex_unlock(&myQueue->mutex);
}

void mymsqdestroy(MyQueue* myQueue) {
    destroy(myQueue->queue);
    sem_destroy(&myQueue->full_sem);
    sem_destroy(&myQueue->empty_sem);
    pthread_mutex_destroy(&myQueue->mutex);
    free(myQueue);
}

int mymsgput(MyQueue* myQueue, char* msg) {
    sem_wait(&myQueue->full_sem);
    pthread_mutex_lock(&myQueue->mutex);
    if (myQueue->dropped) {
        sem_post(&myQueue->full_sem);
        pthread_mutex_unlock(&myQueue->mutex);
        return 0;
    }

    int msg_len = strlen(msg);
    int new_msg_len;
    if (msg_len > 80) {
        new_msg_len = 80;
    }
    else {
        new_msg_len = msg_len;
    }

    char* str = (char*)calloc(new_msg_len + 1, sizeof(char));
    strncpy(str, msg, new_msg_len);
    str[new_msg_len] = 0;

    push(myQueue->queue, str);

    sem_post(&myQueue->empty_sem);
    pthread_mutex_unlock(&myQueue->mutex);
    return msg_len;
}

int mymsgget(MyQueue* myQueue, char *buf, size_t bufsize) {
    sem_wait(&myQueue->empty_sem);
    pthread_mutex_lock(&myQueue->mutex);
    if (myQueue->dropped) {
        sem_post(&myQueue->empty_sem);
        pthread_mutex_unlock(&myQueue->mutex);
        return 0;
    }

    char* tmp = pop(myQueue->queue);
    tmp[bufsize] = 0;
    strcpy(buf, tmp);

    sem_post(&myQueue->full_sem);
    pthread_mutex_unlock(&myQueue->mutex);
    return  strlen(buf);
}