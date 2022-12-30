#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void* sort_by_length(const char* line) {
    if (0 != usleep(strlen(line) * 5500)) {
        perror("Error in usleep");
        return (void*) EXIT_FAILURE;
    }
    printf("%s", line);
    return (void*) EXIT_SUCCESS;
}

int main() {
    int total_lines = 100;
    char** lines = (char**) malloc(sizeof(char**) * total_lines);
    if (lines == NULL) {
        perror("malloc");
        pthread_exit((void*) EXIT_FAILURE);
    }
    for (int i = 0; i < total_lines; i++) {
        lines[i] = NULL;
    }

    printf("Write some lines:\n");
    int lines_count = 0;
    size_t line_size = 0;
    while (getdelim(lines + lines_count, &line_size, '\n', stdin)) {
        if (lines_count == total_lines) {
            printf("Limit is 100 lines\n");
            break;
        } else if (strlen(lines[lines_count]) == 1) {
            break;
        }

        lines_count++;
        line_size = 0;
    }

    printf("Sorted lines:\n");
    pthread_t* threads = (pthread_t*) malloc(lines_count * sizeof(pthread_t));
    if (threads == NULL) {
        perror("malloc");
        pthread_exit((void*) EXIT_FAILURE);
    }

    int return_value;
    for (int i = 0; i < lines_count; i++) {
        return_value = pthread_create(threads + i, NULL, (void*) sort_by_length, (void*) lines[i]);
        if (0 != return_value) {
            fprintf(stderr, "Cannot create a thread: %s", strerror(return_value));
            pthread_exit((void*) EXIT_FAILURE);
        }
    }

    for (int i = 0; i < lines_count; i++) {
        return_value = pthread_join(threads[i], NULL);
        if (0 != return_value) {
            fprintf(stderr, "Cannot join a thread: %s", strerror(return_value));
            pthread_exit((void*) EXIT_FAILURE);
        }
    }

    free(lines);
    free(threads);
    pthread_exit((void*) EXIT_SUCCESS);
}
