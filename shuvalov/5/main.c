#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CLEANUP_POP_EXECUTE 1

void cleanup_handler(void* arg) {
    printf("Called clean-up handler\n");
}

void* print_text() {
    pthread_cleanup_push(cleanup_handler, NULL) ;
            for (int i = 0; i < 100000; i++) {
                printf("Line %d\n", i);
            }
    pthread_cleanup_pop(CLEANUP_POP_EXECUTE);
    return NULL;
}

int main() {
    pthread_t new_thread;
    int return_value = pthread_create(&new_thread, NULL, print_text, NULL);
    if (0 != return_value) {
        fprintf(stderr, "Cannot create the thread: %s", strerror(return_value));
        pthread_exit((void*) EXIT_FAILURE);
    }

    sleep(2);
    return_value = pthread_cancel(new_thread);
    if (0 != return_value) {
        fprintf(stderr, "Error while canceling the thread: %s\n", strerror(return_value));
    }

    void* thread_exit_status = NULL;
    return_value = pthread_join(new_thread, &thread_exit_status);
    if (0 != return_value) {
        fprintf(stderr, "Cannot join a thread: %s\n", strerror(return_value));
        pthread_exit((void*) EXIT_FAILURE);
    }

    if (thread_exit_status == PTHREAD_CANCELED) {
        printf("Thread was canceled\n");
    } else {
        printf("Thread completed by pthread_exit\n");
    }

    pthread_exit((void*) EXIT_SUCCESS);
}
