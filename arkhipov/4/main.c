#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

void* line_printer() {
    while (1) {
        printf("word ");
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

    sleep(2);
    error = pthread_cancel(thread);
    if (error != 0) {
        fprintf(stderr, "Error while cancel thread: %s\n", strerror(error));
        pthread_exit(NULL);
    }

    void* res;
    error = pthread_join(thread, &res);
    if (error != 0) {
        fprintf(stderr, "Error while join thread: %s\n", strerror(error));
        pthread_exit(NULL);
    }
    

    if (res != PTHREAD_CANCELED) {
        fprintf(stderr, "Error: thread isn't canceled.\n");
        pthread_exit(NULL);
    }

    printf("\nchild thread canceled and joined\n");

    pthread_exit(NULL);
}
