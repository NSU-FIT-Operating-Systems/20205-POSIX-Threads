#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void *thread_sub() {
	pthread_setcanceltype()
	int e = -1;
	for(;;) {
		//printf("thread: %d\n", iteration);
		//pthread_testcancel();
		e++;
	}
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
	sleep(2);
	pthread_cancel(thread_10);
	printf("before joining\n");
	if(pthread_join(thread_10, NULL) != 0) {
		printf("thread joining failed\n");
		pthread_attr_destroy(&attrs);
		pthread_exit(NULL);
	}
	printf("subthread was killed\n");
	pthread_attr_destroy(&attrs);
	pthread_exit(NULL);
}
