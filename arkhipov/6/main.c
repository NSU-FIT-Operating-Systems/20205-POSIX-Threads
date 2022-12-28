#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

double SLEEP_SCALAR;


int parse_int(char* str) {
    char * endpoint;
    int res = (int)strtol(str, &endpoint, 10);

    if (*endpoint != '\0') {
        fprintf(stderr, "Expect lines count, got: %s\n", str);
        exit(1);
    }
    return res;
}

void *print_line(void* arg) {
    char* line = (char*)arg;
    sleep(SLEEP_SCALAR * strlen(line));
    printf("%s\n", line);
    return NULL;
}

int main(int argc, char* argv[]) {

    if (argc != 3) {
        fprintf(stderr, "Specify exactly 2 args: lines_count, sleep_time_coefficient \n");
        return 1;
    }

    int lines_count = parse_int(argv[1]);
    SLEEP_SCALAR = parse_int(argv[2]) / 1000.0;
    char lines[lines_count][BUFSIZ];

    for (int i = 0; i < lines_count; ++i) {
        scanf("%s", lines[i]);
    }

    int error;
    pthread_t threads[lines_count];
    for (int i = 0; i < lines_count; ++i) {
        error = pthread_create(&(threads[i]), NULL, print_line, (void*)(lines[i]));
        if (error != 0) {
            fprintf(stderr, "Error while create thread: %s\n", strerror(error));
            pthread_exit(NULL);
        }
    }

    for (int i = 0; i < lines_count; ++i) {
        error = pthread_join(threads[i], NULL);
        if (error != 0) {
            fprintf(stderr, "Error while join thread: %s\n", strerror(error));
            pthread_exit(NULL);
        }
    }

    pthread_exit(NULL);
}
