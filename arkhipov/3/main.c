#include <stdio.h>
#include <pthread.h>
#include <string.h>

typedef struct {
    char** lines;
    int count;
} TLines;

void* line_printer(void* arg) {
    TLines* lines_struct = (TLines*)arg;
    char** lines = lines_struct->lines;
    int count = lines_struct->count;
    for (int i = 0; i < count; ++i) {
        printf("%s", lines[i]);
    }
    return NULL;
}

int main() {

    pthread_t threads[4];
    int error;

    char* lines_1[1] = {"[1] I am 1st process\n"};
    char* lines_2[2] = {"[2] I am 2nd process\n", "[2] I have 2 lines\n"};
    char* lines_3[3] = {"[3] I am 3rd process\n", "[3] I have 3 lines\n", "[3] Hello World\n"};
    char* lines_4[4] = {"[4] I am 4th process\n", "[4] I have 4 lines\n", "[4] Hello\n", "[4] World\n"};
    TLines lines[4] = {
        {lines_1, 1},
        {lines_2, 2},
        {lines_3, 3},
        {lines_4, 4}
    };

    for (int i = 0; i < 4; ++i) {
        error = pthread_create(&threads[i], NULL, line_printer, (void*)(&lines[i]));
        if (error != 0) {
            fprintf(stderr, "Error while create thread: %s\n", strerror(error));
            pthread_exit(NULL);
        }
    }

    for (int i = 0; i < 4; ++i) {
        error = pthread_join(threads[i], NULL);
        if (error != 0) {
            fprintf(stderr, "Error while join thread: %s\n", strerror(error));
            pthread_exit(NULL);
        }
    }

    pthread_exit(NULL);
}
