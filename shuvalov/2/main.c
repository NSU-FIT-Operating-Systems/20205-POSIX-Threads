#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

//Questions:
//interface
//black box
//why does it work

void *print_ten_lines(void *thread_num) {
    for (int i = 0; i < 50; i++) {
        printf("Thread %d: line %d\n", *(int *) thread_num, i);
    }
    return (void *) EXIT_SUCCESS;
}

int main() {
    pthread_t new_thread;
    int *new_tread_num = (int *) malloc(sizeof(int));
    *new_tread_num = 2;
    int return_value = pthread_create(&new_thread, NULL, print_ten_lines, (void *) new_tread_num);
    if (0 != return_value) {
        fprintf(stderr, "Cannot create a thread: %s", strerror(return_value));
        pthread_exit((void *) EXIT_FAILURE);
    }

    return_value = pthread_join(new_thread, NULL);
    if (0 != return_value) {
        fprintf(stderr, "Cannot join a thread: %s", strerror(return_value));
        pthread_exit((void *) EXIT_FAILURE);
    }

    int old_thread_num = 1;
    print_ten_lines((void *) &old_thread_num);
    pthread_exit((void *) EXIT_SUCCESS);
}
