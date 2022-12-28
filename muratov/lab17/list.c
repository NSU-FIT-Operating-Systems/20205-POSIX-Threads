#include "list.h"

int add_string();

int add_string(List *list, char *str) {
	pthread_mutex_lock(&list_mutex);
	Item *item = malloc(sizeof(char) * strlen(str) + 1);
	if(item == NULL) {
		perror("malloc: failed to allocate memory for new string item");
		pthread_mutex_unlock(&list_mutex);
		return 0;
	}
	item->str = str;
	if(list->end == NULL) {
		list->end = item;
		item->next = NULL;
	}
	else {
		item->next = list->begin;
	}
	list->begin = item;
	list->size++;
	pthread_mutex_unlock(&list_mutex);
	return 1;
}


void sort(List *list) {
	pthread_mutex_lock(&list_mutex);
	for(int pos = 0; pos < list->size - 1; pos++) {
		Item *item = list->begin;
		for(int shift = pos; shift < list->size - 1; shift++) {
			if(strcmp(item->str, item->next->str) > 0) {
				char *tmp = item->str;
				item->str = item->next->str;
				item->next->str = tmp;
			}
			item = item->next;
		}
	}
	pthread_mutex_unlock(&list_mutex);
}

void print_list(const List *list) {
	pthread_mutex_lock(&list_mutex);
	for(Item *iter = list->begin; iter != NULL; iter = iter->next) {
		printf("%s\n", iter->str);
	}
	pthread_mutex_unlock(&list_mutex);
}

void clear_list(List *list) {
	pthread_mutex_lock(&list_mutex);
	for(Item *iter = list->begin; iter != NULL;) {
		Item *next = iter->next;
		free(iter->str);
		free(iter);
		iter = next;
	}
	list->size = 0;
	list->begin = list->end = NULL;
	pthread_mutex_unlock(&list_mutex);
}
