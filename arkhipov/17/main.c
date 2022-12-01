#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include "LineListNode.h"

#define LINE_SIZE (80)
#define MEM_ERROR (1)
#define INTERRUPTED (2)

LineListNode head = {NULL, NULL};

pthread_mutex_t mutex;

int running = 1;

int parse_int(char* str) {
    char * endpoint;
    int res = (int)strtol(str, &endpoint, 10);

    if (*endpoint != '\0') {
        fprintf(stderr, "Expect lines count, got: %s\n", str);
        exit(1);
    }
    return res;
}

void sig_handler(int id) {
    if (id == SIGINT) {
        running = 0;    
    }
}

// function for sorting thread
void* sort_iteratively(void* arg) {    
    int sort_delay = *((int*)(arg));
    while (running) {
        usleep(sort_delay);
        pthread_mutex_lock(&mutex);
        sort_list(head.next);
        pthread_mutex_unlock(&mutex);
    }
    pthread_exit(NULL);
}

int handle_input() {
    LineListNode* cursor = &head;
    while (running) {
        char* line = malloc(LINE_SIZE * sizeof(line));
        LineListNode* next = malloc(sizeof(next));

        if (line == NULL || next == NULL) {
            return MEM_ERROR;
        }
        int line_size = read(STDIN_FILENO, line, LINE_SIZE);
        if (line[line_size - 1] == '\n') {
            line[line_size - 1] = '\0';
            line_size--;
        }

        if (line_size == 0) {
            // print list
            pthread_mutex_lock(&mutex);
            print_list(head.next);
            pthread_mutex_unlock(&mutex);
        } else {
            // append str
            next->value = line;
            next->next = NULL;
            pthread_mutex_lock(&mutex);
            cursor->next = next;
            cursor = cursor->next;
            pthread_mutex_unlock(&mutex);
        }
    }
    return INTERRUPTED;
}


int main(int argc, char* argv[]) {
    
    if (argc != 2) {
        fprintf(stderr, "Specify exact 1 arg: time between sortings (in miliseconds).\n");
        pthread_exit(NULL);
    }
    
    // delay for sort worker (in ms.)
    int sort_delay = parse_int(argv[1]); 

    //set interrupt handler 
    struct sigaction sa;
    sa.sa_handler = sig_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGALRM, &sa, NULL);

    int err = pthread_mutex_init(&mutex, NULL);
    if (err != 0) {
        fprintf(stderr, "Error while init mutex: %s\n", strerror(err));
        return 1;
    }

    pthread_t sorting_thread;
    err = pthread_create(&sorting_thread, NULL, sort_iteratively, (void*)(&sort_delay));
    if (err != 0) {
        fprintf(stderr, "main: the function PTHREAD_CREATE(3C) ended with an error: %s\n", strerror(err));
    }
    
    int status = handle_input();
    if (status == MEM_ERROR) {
        fprintf(stderr, "Error while alocating new memory in heap\n");
    } else if (status == INTERRUPTED) {
        fprintf(stderr, "\nInterrupted.\n");
    }

    printf("Gracefull shutdown:\n- wait sort worker end\n");

    err = pthread_join(sorting_thread, NULL);
    if (err != 0) {
        fprintf(stderr, "Error while joining thread: %s\n", strerror(err));
        return 1;
    }
   
    printf("- delete lines list\n");
    delete_list(head.next);
   
    printf("- destroy sync tools\n");
    err = pthread_mutex_destroy(&mutex);
    if (err != 0) {
        fprintf(stderr, "Error while destroy mutex: %s\n", strerror(err));
    }

    pthread_exit(NULL);
}
