#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#define LINES_COUNT 10

pthread_mutexattr_t attr[3];
pthread_mutex_t mutex[3];
int barrier_cnt = 0;
int ping = 0;


void barrier_for_two(int idx) {
    // check in barrier
    int order;
    pthread_mutex_lock(&(mutex[2]));
    
    barrier_cnt = (barrier_cnt + 1) % 2; 
    order = barrier_cnt;
    
    if (order == 1) { // state FISRT
        
    	pthread_mutex_unlock(&(mutex[2]));     // allow neighbour to enter barrier
        
	    pthread_mutex_lock(&(mutex[1 - idx])); // locks on neighbour mutex
					                           // guaranteed have (1 - idx) mutex
					       
	    pthread_mutex_unlock(&(mutex[idx]));   // release neighbour
    } else {        // state SECOND 
	    pthread_mutex_unlock(&(mutex[idx]));   // release neighbour

        pthread_mutex_lock(&(mutex[1 - idx])); // locks on neighbour mutex
    
        pthread_mutex_unlock(&(mutex[2]));     // allow neighbour to enter barrier
					                           // only after locking (1 - idx)
					                           // so guaranteed have (1 - idx) mutex
    }
}

void main_th() {
    int idx = 0;

    // initialisation
    while(1) {
	    if (ping == 1) break;
        sleep(0.1f);
    }
    
    for (int i = 0; i < LINES_COUNT; ++i) {
        barrier_for_two(idx);
	    if (i % 2 == 0) {
	        printf("main thread: %d\n", i);
    	}
    	idx = 1 - idx;
    }

    pthread_mutex_unlock(&(mutex[idx]));    
}

void* child_th() {
    int idx = 1;

    // initialisation
    pthread_mutex_lock(&(mutex[idx]));
    ping = 1;

    // work
    for (int i = 0; i < LINES_COUNT; ++i) {	
        barrier_for_two(idx);
    	if (i % 2 == 1) {
	        printf("child thread: %d\n", i);
	    }
	    idx = 1 - idx;
    }

    // after barrier_for_two this thread
    // have exactly 1 mutex: (1 - idx)
    // we change idx to (1 - idx)
    // so thread have exactly mutex[idx]
    pthread_mutex_unlock(&(mutex[idx]));

    return NULL;
}

// destroy functions

void destroy_my_mutex_attr(pthread_mutexattr_t* attr_) {
    int err = pthread_mutexattr_destroy(attr_);
    if (err != 0) {
       printf("main: the function PTHREAD_MUTEXATTR_DESTROY(3C) ended with an error: %s\n", strerror(err));
    }
}

void destroy_all_mutex_attrs() {
    for (int i = 0; i < 3; ++i) {
        destroy_my_mutex_attr(&(attr[i]));
    }
}

void destroy_my_mutex(pthread_mutex_t* mutex_) {
    int err = pthread_mutex_destroy(mutex_); 
    if (err != 0) {
        printf("main: the function PTHREAD_MUTEX_DESTROY(3C) ended with an error: %s\n", strerror(err));
    }
}

void destroy_all_mutexes() {
    for (int i = 0; i < 3; ++i) {
        destroy_my_mutex(&(mutex[i]));
    }
}

int main() {
    int err;

    // init all mutexes and attrs
    for (int i = 0; i < 3; ++i) {
        if ((err = pthread_mutexattr_init(&(attr[i])) != 0)) {
            printf("main: the function PTHREAD_MUTEXATTR_SETTYPE(3C) ended with an error: %s\n", strerror(err));
            return EXIT_FAILURE;
        }
    
        if ((err = pthread_mutexattr_settype(&(attr[i]), PTHREAD_MUTEX_ERRORCHECK)) != 0) {
            printf("main: the function PTHREAD_MUTEXATTR_SETTYPE(3C) ended with an error: %s\n", strerror(err));
            return EXIT_FAILURE;
        }

        if ((err = pthread_mutex_init(&(mutex[i]), &(attr[i]))) != 0) {
            printf("main: the function PTHREAD_MUTEX_INIT(3C) ended with an error: %s\n", strerror(err));
            return EXIT_FAILURE;
        }
    }

    pthread_mutex_lock(&(mutex[0]));

    pthread_t thread;
    err = pthread_create(&thread, NULL, child_th, NULL);
    if (err != 0) {
        printf("main: the function PTHREAD_CREATE(3C) ended with an error: %s\n", strerror(err));
        destroy_all_mutexes();
        destroy_all_mutex_attrs();
        return EXIT_FAILURE;
    }

    main_th();

    err = pthread_join(thread, NULL);    
    if (err != 0) {
        printf("main: the function PTHREAD_JOIN(3C) ended with an error: %s\n", strerror(err));
    }

    destroy_all_mutexes();
    destroy_all_mutex_attrs();
    pthread_exit(NULL);
}
