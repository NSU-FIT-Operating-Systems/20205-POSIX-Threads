#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <string.h>

pthread_mutex_t mutex;
pthread_cond_t condVar;

bool isMainOutput = true;
bool err = false;

void errorMessage(char* errorMsg) {
    perror(errorMsg);
}

void* function(int threadIndex) {
    for (int i = 0; i < 10; ++i) {
        if (err) {
            return (void*) EXIT_FAILURE;
        }
        if (pthread_mutex_lock(&mutex)) {
            errorMessage("Error lock mutex");
            err = true;
            return (void*) EXIT_FAILURE;
    	}
        while (!err && ((isMainOutput && threadIndex) || (!isMainOutput && !threadIndex))) {
            if (pthread_cond_wait(&condVar, &mutex)){
            	return (void*) EXIT_FAILURE;
            }
        }
        isMainOutput = !isMainOutput;
        if (threadIndex) {
            printf("second - %d\n", i);
        }
        else {
            printf("first - %d\n", i);
        }
        if (pthread_mutex_unlock(&mutex)) {
            errorMessage("Error unlock mutex");
            err = true;
            return (void*) EXIT_FAILURE;
        }
        pthread_cond_signal(&condVar);
    }
    return (void*) EXIT_SUCCESS;
}

int main() {
    pthread_t thread;
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    if (pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK)) {
        perror("Error creating attributes\n");
        pthread_exit(NULL);
    }
    if (pthread_mutex_init(&mutex, &attr)) {
        errorMessage("Error creating mutex");
        pthread_exit(NULL);
    }
    pthread_mutexattr_destroy(&attr);
    
    if (pthread_cond_init(&condVar, NULL)) {
    	errorMessage("Error creating mutex");
    	pthread_mutex_destroy(&mutex);
        pthread_exit(NULL);
    }
    
    if (pthread_create(&thread, NULL, (void* (*)(void*)) function, (void*) 1)) {
        errorMessage("pthread_create failed");
        pthread_mutex_destroy(&mutex);
        pthread_cond_destroy(&condVar);
        pthread_exit(NULL);
    }
    function(0);
    if (pthread_join(thread, NULL)){
        errorMessage("Error waiting thread");
    }
    pthread_mutex_destroy(&mutex);
    pthread_cond_destroy(&condVar);
    pthread_exit(NULL);
}
