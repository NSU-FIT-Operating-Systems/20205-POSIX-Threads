#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

void* line_printer() {
    for (int i = 0; i < 5; ++i) {
        printf("child thread line %d.\n", i);
    }
    return NULL;
}

int main() {

    pthread_t thread;
    int error;
    
    error = pthread_create(&thread, NULL, line_printer, NULL);
    if (error != 0) {
        fprintf(stderr, "Error while create thread: %s\n", strerror(error));
        pthread_exit(NULL);
    }

    for (int i = 0; i < 5; ++i) {
        printf("main thread line %d.\n", i);
    }

    pthread_exit(0);
}
