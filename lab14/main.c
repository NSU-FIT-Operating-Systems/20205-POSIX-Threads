#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <semaphore.h>
#include <string.h>

sem_t sem[2];

void semDestroy(int index) {
    for (int i = 0; i < index; ++i) {
        sem_destroy(&sem[i]);
    }
}

void stop(char* errorMsg) {
    perror(errorMsg);
}

int semWait(int index) {
    if (sem_wait(&sem[index])) {
        stop("Error sem wait");
        return -1;
    }
}

int semPost(int index) {
    if (sem_post(&sem[index])) {
        stop("Error sem post");
        return -1;
    }
}

void* function() {
    char buf[100];
    int err;
    for(int i = 0; i < 10; ++i) {
        if (semWait(1)) {
            break;
        }
        printf("second - %d\n", i);
        if (semPost(0)) {
            break;
        }
    }
}

void mainThread() {
     for(int i = 0; i < 10; ++i) {
     	if (semWait(0)) {
            break;
        }
        printf("first - %d\n", i);
        
        if (semPost(1)) {
            break;
        }
    }
}

int main() {
    pthread_t thread;
    
    sem_init(&sem[0], 1, 1);
    sem_init(&sem[1], 1, 0);

    if(pthread_create(&thread, NULL, function, NULL)) {
        stop("pthread_create failed");
    }

    mainThread();

    if(pthread_join(thread, NULL)){
        stop("Error waiting thread");
    }
    semDestroy(2);

    return EXIT_SUCCESS;
}
