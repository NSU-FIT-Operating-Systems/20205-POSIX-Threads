#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>

pthread_mutex_t mutex1, mutex2, startMutex;

void* function(void* param) {
    char buf[100];
    int err;
    if ((err = pthread_mutex_lock(&startMutex)) != 0) {
            strerror_r(err, buf, 100);
            fprintf(stderr, "11%s\n",buf);
            exit(1);
       }
    for (int i = 0; i < 10; ++i) {
        if ((err = pthread_mutex_lock(&mutex2)) != 0) {
            strerror_r(err, buf, 100);
            fprintf(stderr, "12%s\n",buf);
            exit(1);
       }
        printf("second - %d\n", i);

        if ((err = pthread_mutex_unlock(&mutex1)) != 0) {
            strerror_r(err, buf, 100);
            fprintf(stderr, "13%s\n",buf);
            exit(1);
       }

    }
    pthread_mutex_unlock(&startMutex);
    pthread_mutex_unlock(&mutex2);
    pthread_exit(NULL);
}

int main(int arc, char** argv) {

    char buf[100];
    int err;
    pthread_t thread;
    pthread_mutexattr_t attr;
    if ((err = pthread_mutexattr_init(&attr)) != 0) {
            strerror_r(err, buf, 100);
            fprintf(stderr, "4%s\n", buf);
            exit(1);
    }
    /*if ((err = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK)) != 0) {
            strerror_r(err, buf, 100);
            fprintf(stderr, "5%s\n", buf);
            exit(1);
    }*/

    if ((err = pthread_mutex_init(&startMutex, &attr)) != 0) {
            strerror_r(err, buf, 100);
            fprintf(stderr, "1%s\n", buf);
            exit(1);
    }

    if ((err = pthread_mutex_init(&mutex1, &attr)) != 0) {
            strerror_r(err, buf, 100);
            fprintf(stderr, "2%s\n", buf);
            exit(1);
    }

    if ((err = pthread_mutex_init(&mutex2, &attr)) != 0) {
            strerror_r(err, buf, 100);
            fprintf(stderr, "3%s\n", buf);
            exit(1);
    }
    pthread_mutexattr_destroy(&attr);

    if ((err = pthread_mutex_lock(&startMutex)) != 0) {
            strerror_r(err, buf, 100);
            fprintf(stderr, "56%s\n",buf);
            exit(1);
       }
    err = pthread_create(&thread, NULL, function, NULL);

    if (err) {
    	    strerror_r(err, buf ,100);
            fprintf(stderr, "33%s\n", buf);
            exit(1);
    }
    if ((err = pthread_mutex_lock(&mutex2)) != 0) {
            strerror_r(err, buf, 100);
            fprintf(stderr, "9%s\n",buf);
            exit(1);
       }
    if ((err = pthread_mutex_unlock(&startMutex)) != 0) {
            strerror_r(err, buf, 100);
            fprintf(stderr, "8%s\n", buf);
            exit(1);
    }

    for (int i = 0; i < 10; ++i) {
        if ((err = pthread_mutex_lock(&mutex1)) != 0) {
            strerror_r(err, buf, 100);
            fprintf(stderr, "7%s\n",buf);
            exit(1);
       }
        printf("first - %d\n", i);
        if ((err = pthread_mutex_unlock(&mutex2)) != 0) {
            strerror_r(err, buf, 100);
            fprintf(stderr, "1%s\n", buf);
            exit(1);
        }
    }
    pthread_exit(NULL);
}
