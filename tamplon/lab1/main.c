#include <stdio.h>
#include <pthread.h>
#include <string.h>

void* PrintSampleText(void* args) {
	for (int i = 0; i < 10; ++i) {
		printf("%d Hello from other thread\n", i);
	}
	return NULL;
}

void PrintSampleTextFromMainThread() {
	for (int i = 0; i < 10; ++i) {
		printf("%d Hello from main thread\n", i);
	}
}

int main() {
	pthread_t pthread;
	int err = pthread_create(&pthread, NULL, PrintSampleText, NULL);
	if (err != 0) {
		fprintf(stderr,"Cannot create a thread %s\n", strerror(err));
		return 1;
	}
	PrintSampleTextFromMainThread();
	pthread_exit(NULL);
}


