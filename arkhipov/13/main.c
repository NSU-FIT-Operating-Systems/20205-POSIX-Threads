#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

pthread_mutex_t mutex;

pthread_cond_t cv;

int i = 0;

void inc_i(char* str) {
    pthread_mutex_lock(&mutex);

    printf("%d: %s\n", i, str);
    i += 1;
    pthread_cond_signal(&cv);

    pthread_mutex_unlock(&mutex);
}

void wait_i(int id) {
    pthread_mutex_lock(&mutex);
    while (i % 2 != id % 2)
        pthread_cond_wait(&cv, &mutex);
    pthread_mutex_unlock(&mutex);
}

void child_routine(char* str, int n) {
    while (i < n) {
	wait_i(1);
	inc_i(str);
    }
}

void main_routine(char* str, int n) {
    while (i < n) {
	inc_i(str);
        wait_i(0);
    }
}

void* start_routine(void* ptr) {
    char* str = (char*) ptr;
    child_routine(str, 10);
    return NULL;
}

// destroy functions
void destroy_my_mutex(pthread_mutex_t* mutex) {
    int err = pthread_mutex_destroy(mutex);
    if (err != 0) {
        printf("main: the function PTHREAD_MUTEX_DESTROY(3C) ended with an error: %s\n", strerror(err));
    }
}

void destroy_my_cv(pthread_cond_t* cv) {
    int err = pthread_cond_destroy(cv);
    if (err != 0) {
        printf("main: the function PTHREAD_COND_DESTROY(3C) ended with an error: %s\n", strerror(err));
    }
}

int main() {
    pthread_t thread;
    char* str_main = "master thread";
    char* str_child = "child thread";
    int err;
     
    if ((err = pthread_mutex_init(&mutex, NULL)) != 0) {
        printf("main: the function PTHREAD_MUTEX_INIT(3C) ended with an error: %s\n", strerror(err));
        return EXIT_FAILURE;
    }
    
    if ((err = pthread_cond_init(&cv, NULL)) != 0) {
        printf("main: the function PTHREAD_COND_INIT(3C) ended with an error: %s\n", strerror(err));
	destroy_my_mutex(&mutex);
	return EXIT_FAILURE;
    }

    err = pthread_create(&thread, NULL, start_routine, (void*)str_child); 
    if (err != 0) {
        printf("main: the function PTHREAD_CREATE(3C) ended with an error: %s\n", strerror(err));
        destroy_my_mutex(&mutex);
	destroy_my_cv(&cv);
        pthread_exit(NULL);
    }

    main_routine(str_main, 10);
    
    err = pthread_join(thread, NULL);
    if (err != 0) {    
        printf("main: the function PTHREAD_JOIN(3C) ended with an error: %s\n", strerror(err));
    }

    destroy_my_mutex(&mutex);
    destroy_my_cv(&cv);

    pthread_exit(NULL);
}
