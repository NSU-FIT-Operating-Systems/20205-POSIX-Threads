#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

pthread_mutexattr_t attr;
pthread_mutex_t m[3];

void destroyMutex(int index) {
    for (int i = 0; i < index; ++i) {
        pthread_mutex_destroy(&m[i]);
    }
}

void stop(char* errorMsg) {
    perror(errorMsg);
}

int lockMutex(int index) {
    if (pthread_mutex_lock(&m[index])) {
        stop("Error lock mutex");
        return -1;
    }
}

int unlockMutex(int index) {
    if (pthread_mutex_unlock(&m[index])) {
        stop("Error unlock mutex");
        return -1;
    }
}

void initMutexes() {
    pthread_mutexattr_init(&attr);
    //if (pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK)) {
        //perror("Error creating attributes\n");
        //pthread_exit(NULL);
    //}

    for (int i = 0; i < 3; ++i) {
        if (pthread_mutex_init(&m[i], &attr)) {
            destroyMutex(i);
            perror("Error creating mutex");
            pthread_exit(NULL);
        }
    }
}

void* function() {
    int n = 0;
    int prev_n = n;
    if (lockMutex(1)) {
            return NULL;
    }
    for (int i = 0; i < 10; ++i) {
        if (lockMutex(n)) {
            break;
        }
        printf("second - %d\n", i);
        if (unlockMutex((n + 1) % 3)) {
            break;
        }
        prev_n = n;
        n = (n + 2) % 3;
    }
    for (int i = 0; i < 3; ++i) {
    	if (unlockMutex(i)) {
            continue;
        }
    }
    return NULL;
}

void first() {
    int n = 0;
    int prev_n = n;
    for (int i = 0; i < 10; ++i) {
        printf("first - %d\n", i);
	if (unlockMutex(n)) {
            break;
        }
        if (lockMutex((n + 1) % 3)) {
            break;
        }
        prev_n = n;
        n = (n + 2) % 3;
    }
    for (int i = 0; i < 3; ++i) {
    	if (unlockMutex(i)) {
            continue;
        }
    }
}

int main(int arc, char** argv) {
    pthread_t thread;

    initMutexes();
    lockMutex(0);
    lockMutex(2);

    if (pthread_create(&thread, NULL, function, NULL)) {
        stop("Error creating thread");
    }

    if (sleep(1)) {
        stop("Sleep was interrupted");
    }

    first();

    if (pthread_join(thread, NULL)) {
        stop("Error waiting thread");
    }

    destroyMutex(3);
    return EXIT_SUCCESS;
}
