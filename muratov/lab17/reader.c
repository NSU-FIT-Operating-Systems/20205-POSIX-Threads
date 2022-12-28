#include "list.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define SORT_DELAY 5
#define MAX_LENGTH 80

List list;
pthread_mutex_t list_mutex;

void *sort_thread() {
	while(1) {
		sleep(SORT_DELAY);
		sort(&list);
	}
	pthread_exit(NULL);

}

void clear_all(pthread_t sorter) {
	clear_list(&list);
	pthread_cancel(sorter);
	pthread_join(sorter, NULL);
	pthread_mutex_destroy(&list_mutex);
}

int main() {
	pthread_t sorter;

	if(pthread_mutex_init(&list_mutex, NULL) != 0) {
		perror("failed to init mutex");
		return 1;
	}
	if(pthread_create(&sorter, NULL, sort_thread, NULL) != 0) {
		perror("failed to create thread");
		pthread_mutex_destroy(&list_mutex);
		return 1;
	}
	char buffer[MAX_LENGTH + 1];
	char last_char = '\n';
	while(1) {
		ssize_t count = read(STDIN_FILENO, buffer, MAX_LENGTH);
		if(count < 0) {
			perror("error on reading");
			clear_all(sorter);
			return 1;
		}
		if(strncmp(".\n", buffer, 2) == 0 || count == 0) {
			break;
		}
		if(count == 1) {
			if(last_char == '\n') {
				print_list(&list);
			}
			last_char = '\n';
			continue;
		}
		last_char = buffer[count - 1];
		if(buffer[count - 1] == '\n') {
			buffer[count - 1] = '\0';
		}
		char *copy = (char*) malloc(sizeof(char) * (count + 1));
		if(copy == NULL) {
			perror("failed to allocate memory for new string");
			clear_all(sorter);
			return 1;
		}
		strcpy(copy, buffer);
		if(!add_string(&list, copy)) {
			free(copy);
			clear_all(sorter);
			return 1;
		}
	}
	clear_all(sorter);

}
