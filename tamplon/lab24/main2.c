#include <stdio.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>

typedef struct Detail {
    sem_t* semaphore;
    pthread_t* thread;
    int sleep_time;
    int count;
    struct Detail* components;
    int components_count;
} Detail;

sem_t A_sem; pthread_t A_tid; Detail* A;
sem_t B_sem; pthread_t B_tid; Detail* B;
sem_t module_sem; pthread_t module_tid; Detail* module;
sem_t C_sem; pthread_t C_tid; Detail* C;
sem_t widget_sem; pthread_t widget_tid; Detail* widget;
bool need_to_exit = false;

int destroy_detail(Detail* detail) {
    if (pthread_cancel(*detail->thread) != 0) {
        printf("error at pthread_exit()\n");
        return 1;
    }
    if (sem_destroy(detail->semaphore) != 0) {
        perror("sem_destroy");
        return 1;
    }
    free(detail);
    return 0;
}

int cleanup(int n, ...) {
    va_list list;
    va_start(list, n);
    for (int i = 0; i < n; ++i) {
        Detail* detail = va_arg(list, Detail*);
        if (destroy_detail(detail) != 0) {
            return 1;
        }
    }
    va_end(list);
    return 0;
}

void* detailRoutine(void* args) {
    Detail* detail = (Detail*)args;
    while(!need_to_exit) {
        for (int i = 0; i < detail->components_count; ++i) {
            Detail* component = &detail->components[i];
            if (sem_wait(component->semaphore) != 0) {
                perror("sem_wait");
                need_to_exit = true;
            }
        }
        sleep(detail->sleep_time);
        if (sem_post(detail->semaphore) != 0) {
            perror("sem_post");
            need_to_exit = true;
        }
    }
}

Detail* initDetail(sem_t* sem, pthread_t* tid, int sleep_time, Detail* components, int comp_count) {
    Detail* detail = malloc(sizeof(Detail));
    detail->semaphore = sem;
    detail->thread = tid;
    detail->sleep_time = sleep_time;
    detail->count = 0;
    detail->components = components;
    detail->components_count = comp_count;

    if (sem_init(detail->semaphore, 0, 0) != 0) {
        perror("sem_init");
        return NULL;
    }

    if (pthread_create(detail->thread, NULL, detailRoutine, (void*)detail) != 0) {
        printf("can't create thread\n");
        return NULL;
    }

    return detail;
}

void getValues() {
    sem_getvalue(A->semaphore, &A->count);
    sem_getvalue(B->semaphore, &B->count);
    sem_getvalue(module->semaphore, &module->count);
    sem_getvalue(C->semaphore, &C->count);
    sem_getvalue(widget->semaphore, &widget->count);
}

int main() {
    A = initDetail(&A_sem, &A_tid, 1, NULL, 0);
    if (A == NULL) {
        printf("initial error: A\n");
        pthread_exit(NULL);
    }

    B = initDetail(&B_sem, &B_tid, 2, NULL, 0);
    if (B == NULL) {
        printf("initial error: B\n");
        cleanup(1, A);
        pthread_exit(NULL);
    }

    Detail module_comps[] = {*A, *B};
    module = initDetail(&module_sem, &module_tid, 0, module_comps, 2);
    if (B == NULL) {
        printf("initial error: module\n");
        cleanup(2, B, A);
        pthread_exit(NULL);
    }

    C = initDetail(&C_sem, &C_tid, 3, NULL, 0);
    if (C == NULL) {
        printf("initial error: C\n");
        cleanup(3, module, B, A);
        pthread_exit(NULL);
    }

    Detail widget_comps[] = {*module, *C};
    widget = initDetail(&widget_sem, &widget_tid, 0, widget_comps, 2);
    if (widget == NULL) {
        printf("initial error: widget\n");
        cleanup(4, C, module, B, A);
        pthread_exit(NULL);
    }

    while(widget->count != 5 && !need_to_exit) {
        getValues();
        printf("A: %d | B: %d | module: %d | C: %d | widget: %d\n",
               A->count, B->count, module->count, C->count, widget->count);
    }
    cleanup(5, widget, C, module, B, A);
    pthread_exit(NULL);
}
