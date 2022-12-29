#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void farewell() {
	printf("thread was interrupted\n");
}

void *thread_sub() {
	pthread_cleanup_push(farewell, NULL);
	for(int iteration = 0; iteration < 10000000; iteration++) {
		printf("thread: %d\n", iteration);
	}
	pthread_cleanup_pop(1);
	pthread_exit(NULL);
}

int main() {
	pthread_attr_t attrs;
	pthread_t thread_10;
	if(pthread_attr_init(&attrs) != 0) {
		perror("pthread attr init");
		exit(-1);
	}

	if(pthread_create(&thread_10, &attrs, thread_sub, NULL) != 0) {
		perror("pthread init");
		return -1;
	}
	sleep(2);
	pthread_cancel(thread_10);
	pthread_join(thread_10, NULL);
	pthread_attr_destroy(&attrs);
	pthread_exit(NULL);
}
