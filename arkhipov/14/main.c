#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <semaphore.h>

#define ITERATIONS (5)
#define MAX_THREADS_COUNT (100)

int THREADS_COUNT = 2;
sem_t rights_to_print[MAX_THREADS_COUNT];

int parse_int(char* str) {
    char * endpoint;
    int res = (int)strtol(str, &endpoint, 10);

    if (*endpoint != '\0') {
        fprintf(stderr, "Expect lines count, got: %s\n", str);
        exit(1);
    }
    return res;
}

void worker(int idx, int iterations) {
    int neighbour = (idx + 1) % THREADS_COUNT;

    if (idx == 0) {
        sem_post(&(rights_to_print[0])); 
    }
    
    for (int i = 0; i < iterations; ++i) {
        sem_wait(&(rights_to_print[idx]));                      // request rights to print
        int global_iterator = i * THREADS_COUNT + idx;
        printf("[%d] thread line: %d\n", idx, global_iterator); // print it's line
        sem_post(&(rights_to_print[neighbour]));                // give rights to print to another
    }
}

void* thread_entrypoint(void* args) {
    int idx = *((int*)(args));
    worker(idx, ITERATIONS);
    return NULL;
}

void destroy_semaphores() {
    for (int i = 0; i < THREADS_COUNT; ++i) {
        int err = sem_destroy(&(rights_to_print[i]));
        if (err != 0) { 
            fprintf(stderr, "Error while destroy semaphore %d: %s\n", i, strerror(err));
        }
    }
}

int main(int argc, char* argv[]) {

    if (argc > 2) {
        fprintf(stderr, "Specify not more than 1 arg: number of threads (2 by default).\n");
        pthread_exit(NULL);
    }

    if (argc == 2) {
        THREADS_COUNT = parse_int(argv[1]);
        if (THREADS_COUNT > MAX_THREADS_COUNT) {
            fprintf(stderr, "Error: number of threads should be not more than %d\n", MAX_THREADS_COUNT);
            pthread_exit(NULL);
        }
    }

    for (int i = 0; i < THREADS_COUNT; ++i) {
        int err = sem_init(&(rights_to_print[i]), 0, 0);
        if (err != 0) {
            fprintf(stderr, "Error while init semaphore %d: %s\n", i, strerror(err));
            destroy_semaphores();
            pthread_exit(NULL);
        }
    }

    pthread_t workers[THREADS_COUNT];
    int idxs[THREADS_COUNT];
    for (int i = 0; i < THREADS_COUNT - 1; ++i) {
        idxs[i] = i + 1;
        int err = pthread_create(&(workers[i]), NULL, thread_entrypoint, (void*)(&(idxs[i])));
        if (err != 0) {
            fprintf(stderr, "Error while create thread: %s\n", strerror(err));
            destroy_semaphores();
            pthread_exit(NULL);
        }
    }
    worker(0, ITERATIONS);

    for (int i = 0; i < THREADS_COUNT - 1; ++i) {
        int err = pthread_join(workers[i], NULL);
        if (err != 0) {
            fprintf(stderr, "Error while joining thread: %s\n", strerror(err));
            destroy_semaphores();
            pthread_exit(NULL);
        }
    }
    // Cleanup
    destroy_semaphores();
    
    pthread_exit(NULL);
}
