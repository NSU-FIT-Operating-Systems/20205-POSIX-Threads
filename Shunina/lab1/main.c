#include <stdio.h>
#include <pthread.h>

void* function(void *param) {
	for (int i = 0; i < 10; i++) {
		printf("second %d\n", i + 1);
	}
}

int main(int argc, char** argv) {
	pthread_t thread;
	int err = pthread_create(&thread, NULL, function, NULL);
	if (err != 0) {
		printf("Error %d\n", err);
	}
	for (int i = 0; i < 10; i++) {
		printf("first %d\n", i + 1);
	}
	pthread_exit(NULL);
}
