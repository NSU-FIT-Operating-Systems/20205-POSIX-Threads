#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void print10lines(const char *label) {
	for(int iteration = 0; iteration < 10; iteration++) {
		char str[15];
		int count = sprintf(str, "%s: %d\n", label, iteration);
		if(count < 0) {
			perror("write");
			pthread_exit(NULL);
		}
		write(STDOUT_FILENO, str, count);
	}
}

void *thread_sub() {
	print10lines("sub thread");
	pthread_exit(NULL);
}

int main() {
	pthread_attr_t attrs;
	pthread_t thread_10;
	int error = 1;
	if(pthread_attr_init(&attrs) != 0) {
		perror("pthread attr init");
		pthread_exit(&error);
	}

	if(pthread_create(&thread_10, &attrs, thread_sub, NULL) != 0) {
		perror("pthread init");
		pthread_exit(&error);
	}
	print10lines("main");
	//pthread_join(thread_10, NULL);
	pthread_attr_destroy(&attrs);
	pthread_exit(NULL);
}
