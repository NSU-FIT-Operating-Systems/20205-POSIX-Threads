#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>

pthread_mutex_t mutex1, mutex2, startMutex;
void* threadFunction(void* args) {
	(void)args;
	pthread_mutex_lock(&startMutex);
	for (int i = 0; i < 10; ++i) {
		pthread_mutex_lock(&mutex2);
		printf("%d\n", i);
		pthread_mutex_unlock(&mutex1);
	}
	pthread_exit(NULL);
}

int main() {
	pthread_t thread;
	if (pthread_mutex_init(&startMutex, NULL) != 0) {
		perror("Error creating mutex");
		return EXIT_FAILURE;
	}
	if (pthread_mutex_init(&mutex1, NULL) != 0) {
		perror("Error creating mutex");
		return EXIT_FAILURE;
	}
	if (pthread_mutex_init(&mutex2, NULL) != 0) {
		perror("Error creating mutex");
		return EXIT_FAILURE;
	}
	pthread_mutex_lock(&startMutex);
	int err = pthread_create(&thread, NULL, threadFunction, NULL);
	if (err != 0) {
		perror("Error creating thread");
		return EXIT_FAILURE;
	}
	pthread_mutex_lock(&mutex2);
	pthread_mutex_unlock(&startMutex);
	for (int i = 0; i < 10; ++i) {
		pthread_mutex_lock(&mutex1);
		printf("%d\n", i);
		pthread_mutex_unlock(&mutex2);
	}
	pthread_exit(NULL);
}