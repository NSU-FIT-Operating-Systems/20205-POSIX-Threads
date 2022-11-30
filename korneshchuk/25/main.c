#include <stdio.h>
#include <semaphore.h>
#include <pthread.h>
#include <string.h>
#include "my_queue.c"
#include <unistd.h>
#define BUFFER_SIZE 80

void* producer_routine(void* args) {
    MyQueue* queue = (MyQueue*)args;

    while(!queue->dropped) {
        char* str = (char*)calloc(BUFFER_SIZE + 1, sizeof(char));
        for(int i = 0; i < BUFFER_SIZE; i++) {
            str[i] = ((rand() % ('z' - 'A')) + 'A');
        }
        str[BUFFER_SIZE] = 0;

        printf("put %s\n", str);
        int result = mymsgput(queue, str);
        free(str);


        if(result == 0){
            pthread_exit((void*)NULL);
        }

    }

    pthread_exit((void*)NULL);
}

void* consumer_routine(void* args) {
    MyQueue* queue = (MyQueue*)args;

    while(!queue->dropped) {
        char* chr = malloc((BUFFER_SIZE + 1) *  sizeof(char));
        int result = mymsgget(queue, chr, BUFFER_SIZE);
        printf("result is %d. Got %s\n", result, chr);
        free(chr);

        if(result == 0){
            pthread_exit(NULL);
        }
    }
    pthread_exit(NULL);
}

void init_thread(pthread_t* tid, void *(*start_routine) (void *), MyQueue* queue) {
    if (pthread_create(tid, NULL, start_routine, (void*)queue) != 0) {
        printf("can't create thread %lu", *tid);
        pthread_exit(NULL);
    }
}

void init_all_threads(
        pthread_t* producer1,
        pthread_t* producer2,
        pthread_t* consumer1,
        pthread_t* consumer2,
        MyQueue* queue
        ) {
    init_thread(producer1, producer_routine, queue);
    init_thread(producer2, producer_routine, queue);
    init_thread(consumer1, consumer_routine,queue);
    init_thread(consumer2, consumer_routine, queue);
}

void join_thread(const pthread_t* tid) {
    if (pthread_join(*tid, NULL) != 0) {
        printf("can't join thread %lu", *tid);
        pthread_exit(NULL);
    }
}

void join_all_threads(pthread_t* producer1, pthread_t* producer2, pthread_t* consumer1, pthread_t* consumer2) {
    join_thread(producer1);
    join_thread(producer2);
    join_thread(consumer1);
    join_thread(consumer2);
}

int main() {
    MyQueue* queue = malloc(sizeof(MyQueue));
    mymsginit(queue);

    pthread_t producer1;
    pthread_t producer2;
    pthread_t consumer1;
    pthread_t consumer2;
    init_all_threads(&producer1,&producer2, &consumer1, &consumer2, queue);

    sleep(5);
    mymsqdrop(queue);

    join_all_threads(&producer1, &producer2, &consumer1, &consumer2);
    mymsqdestroy(queue);
    pthread_exit(NULL);
}
