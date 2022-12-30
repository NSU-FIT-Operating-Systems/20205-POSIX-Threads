#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define THREADS_NUMBER 4

void *printText(char** text) {
    int i = 0;
    while (text[i] != NULL) {
        printf("%s\n", text[i]);
        i++;
    }
    return NULL;
}

int main() {
    char *text[][5] = {
            {"Thread 1: 1 line",NULL},
            {"Thread 2: 1 line", "Thread 2: 2 line",NULL},
            {"Thread 3: 1 line", "Thread 3: 2 line", "Thread 3: 3 line", NULL},
            {"Thread 4: 1 line", "Thread 4: 2 line", "Thread 4: 3 line", "Thread 4: 3 line", NULL}
    };
    int return_value;
    pthread_t threads[THREADS_NUMBER];
    for (int i = 0; i < THREADS_NUMBER; i++) {
        return_value = pthread_create(threads + i, NULL, (void *(*)(void *)) printText, (void *) text[i]);
        if (0 != return_value) {
            fprintf(stderr, "Cannot create a thread: %s", strerror(return_value));
            pthread_exit((void *) EXIT_FAILURE);
        }
    }

    for (int i = 0; i < THREADS_NUMBER; i++) {
        return_value = pthread_join(threads[i], NULL);
        if (0 != return_value) {
            fprintf(stderr, "Cannot join a thread: %s", strerror(return_value));
            pthread_exit((void *) EXIT_FAILURE);
        }
    }

    pthread_exit((void *) EXIT_SUCCESS);
}
