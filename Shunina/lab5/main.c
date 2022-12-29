#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>

void cleanup_handler(void* param) {
    printf("\nThread canceled. Message from thread\n");
}

void* thread_print(void* param) {
    pthread_cleanup_push(cleanup_handler, NULL);
    int symbol = 0;
    while (1){
        pthread_testcancel();
        printf("%d ", symbol);
        symbol++;
    }
    pthread_cleanup_pop(1);
}

int main() {
    pthread_t thread;
    int err = pthread_create(&thread, NULL, thread_print, NULL);
    if (err) {
        printf("Error №%d: %s\n", err, strerror(err));
    }
    sleep(2);
    pthread_cancel(thread);
    printf("\nBefore join\n");
    int error = pthread_join(thread, NULL);
    if (error != 0) {
	printf("Error %d\n", error);
    }
    pthread_exit(NULL);
}
