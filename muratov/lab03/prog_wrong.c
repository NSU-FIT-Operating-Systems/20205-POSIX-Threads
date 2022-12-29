#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#define POOL_SIZE 4

void print10lines(const char *label) {
	for(int iteration = 0; iteration < 10; iteration++)
		printf("%s: %d\n", label, iteration);
}

void *thread_sub(void *args) {
	print10lines((char*) args);
	pthread_exit(NULL);
}

int main() {
	pthread_attr_t attrs;
	pthread_t threads[POOL_SIZE];
	if(pthread_attr_init(&attrs) != 0) {
		perror("pthread attr init");
		exit(-1);
	}

	for(int index = 0; index < POOL_SIZE; index++) {
		char name[10];
		sprintf(name, "thread%d", index);
		if(pthread_create(threads + index, &attrs, thread_sub, name) != 0) {
			perror("pthread init");
			return -1;
		}
	}

	for(int index = 0; index < POOL_SIZE; index++)
		pthread_join(threads[index], NULL);
	pthread_attr_destroy(&attrs);
	pthread_exit(NULL);
}
