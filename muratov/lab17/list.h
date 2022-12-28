#ifndef LIST_H
#define LIST_H

#include <pthread.h>

#define SORT_DELAY 5000
#define MAX_LENGTH 80

extern pthread_mutex_t list_mutex;

typedef struct Item {
	char *str;
	struct Item *next;
} Item;

typedef struct List {
	Item *begin;
	Item *end;
} List;

int add_string(List *list, char *str);

void sort(List *list);

void print_list(const List *list);

void clear_list(List *list);

#endif //LIST_H
