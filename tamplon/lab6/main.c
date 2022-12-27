#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>

void* new_thread_func(void* args) {
    int len = strlen((char*)args);
    sleep(len * 2);
    printf("%s\n", (char*)args);
}


int main() {
    int N;
    scanf("%d", &N);

    char lines[N][BUFSIZ];
    for (int i = 0; i < N; ++i) {
        scanf("%s", lines[i]);
    }
    printf("______________\n");

    pthread_t threads[N];
    int status;
    for (int i = 0; i < N; ++i) {
        if ((status = pthread_create(&threads[i], NULL, new_thread_func, lines[i])) != 0) {
            fprintf(stderr, "can't create thread %d, status = %s\n", i, strerror(status));
            return 1;
        }
    }

    for (int i = 0; i < N; ++i) {
        if ((status = pthread_join(threads[i], NULL)) != 0) {
            fprintf(stderr, "can't join thread %d, status = %s\n", i, strerror(status));
            return 1;
        }
    }

    return 0;
}
