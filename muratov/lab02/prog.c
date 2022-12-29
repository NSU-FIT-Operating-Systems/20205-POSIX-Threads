#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

void print10lines(const char *label) {
	for(int iteration = 0; iteration < 10; iteration++)
		printf("%s: %d\n", label, iteration);
}

void *thread_sub() {
	print10lines("sub thread");
	pthread_exit(NULL);
}

int main() {
	pthread_attr_t attrs;
	pthread_t thread_10;
	if(pthread_attr_init(&attrs) != 0) {
		perror("pthread attr init");
		pthread_exit(NULL);
	}

	if(pthread_create(&thread_10, &attrs, thread_sub, NULL) != 0) {
		perror("pthread init");
		pthread_exit(NULL);
	}
	print10lines("main");
	pthread_join(thread_10, NULL);
	pthread_attr_destroy(&attrs);
	pthread_exit(NULL);
}
