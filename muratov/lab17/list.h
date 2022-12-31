#ifndef LIST_H
#define LIST_H

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

extern pthread_mutex_t list_mutex;

typedef struct Item {
	char *str;
	struct Item *next;
} Item;

typedef struct List {
	Item *begin;
	int size;
} List;

int add_string(List *list, char *str);

void sort(List *list);

void print_list(const List *list);

void clear_list(List *list);

#endif //LIST_H
