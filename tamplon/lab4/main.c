#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>

void* PrintSampleText(void* args) {
	int i = 0;
	while (i < 100) {
		printf("Sample text number %d \n", i);
		usleep(100000);
		++i;
	}
	return NULL;
}

int main() {
	pthread_t thread;
	int err;
	err = pthread_create(&thread, NULL, &PrintSampleText, NULL);
	if (err != 0) {
		fprintf(stderr, "Cannot create a thread %s\n", strerror(err));
		return 1;
	}
	sleep(2);
	err = pthread_cancel(thread);
	if (err != 0) {
		fprintf(stderr, "Cannot cancel a thread %s\n", strerror(err));
		return 1;
	}
	void* result;
	err = pthread_join(thread, &result);
	if (err != 0) {
		fprintf(stderr, "Cannot join a thread %s\n", strerror(err));
		return 1;
	}
	if (result == PTHREAD_CANCELED) {
		printf("Thread was canceled\n");
	}
	else {
		printf("Thread was not canceled\n");
	}

	return 0;
}


