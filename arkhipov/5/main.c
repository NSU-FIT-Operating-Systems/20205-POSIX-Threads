#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

void cleanup_handler() {
    printf("\nthread canceled\n");
}

void* line_printer() {
    pthread_cleanup_push(cleanup_handler, NULL);
    while (1) {
        printf("word ");
    }
    pthread_cleanup_pop(0);
    return NULL;
}

int main() {

    pthread_t thread;
    int error;

    error = pthread_create(&thread, NULL, line_printer, NULL);
    if (error != 0) {
        fprintf(stderr, "Error while create thread: %s\n", strerror(error));
        return 1;
    }

    sleep(2);
    pthread_cancel(thread);

    void* res;
    error = pthread_join(thread, &res);
    if (error != 0) {
        fprintf(stderr, "Error while join thread: %s\n", strerror(error));
        return 1;
    }

    if (res != PTHREAD_CANCELED) {
        fprintf(stderr, "Error: thread isn't canceled.\n");
        return 1;
    }

    return 0;
}
