#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>

void* thread_print(void* param) {
    int symbol = 0;
    int oldtype;
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);
    while (1) {
        //pthread_testcancel();
        //printf("%d ", symbol);
        symbol++;
    }
}

int main() {
    pthread_t thread;
    int err = pthread_create(&thread, NULL, thread_print, NULL);
    if (err){
        printf("Error №%d: %s\n", err, strerror(err));
    }
    sleep(2);
    pthread_cancel(thread);
    printf("\nBefore join\n");
    int error = pthread_join(thread, NULL);
    if (error != 0) {
	printf("Error %d\n", error);
    }
    printf("\nThread canceled\n");
    pthread_exit(NULL);
}
