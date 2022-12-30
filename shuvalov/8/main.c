#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <math.h>

#define STEPS_NUM 100

long int threads_count;

double calculate_pi_part(const int* thread_number) {
    double* pi_part = (double*) malloc(sizeof(double));
    for (int i = 0; i < STEPS_NUM; i++) {
        *pi_part += 1.0 / ((i * (int) threads_count + *thread_number) * 4.0 + 1.0);
        *pi_part -= 1.0 / ((i * (int) threads_count + *thread_number) * 4.0 + 3.0);
    }

//    printf("Thread %d Pi_part / 4 = %.15lf \n", *thread_number, *pi_part);
    return *pi_part;
}

int main(int argc, char* argv[]) {
    if (argv[1] == NULL) {
        threads_count = 1;
    } else {
        threads_count = strtol(argv[1], NULL, 0);
        if (threads_count == LONG_MIN) {
            perror("Error in strtol");
            pthread_exit((void*) EXIT_FAILURE);
        } else if (threads_count <= 0) {
            fprintf(stderr, "Wrong argument. Threads number should be above zero.\n");
            pthread_exit((void*) EXIT_FAILURE);
        }
    }

    pthread_t* threads = (pthread_t*) malloc(threads_count * sizeof(pthread_t));
    if (threads == NULL) {
        perror("threads malloc");
        pthread_exit((void*) EXIT_FAILURE);
    }
    int* threads_numbers = (int*) malloc(threads_count * sizeof(int));
    if (threads_numbers == NULL) {
        perror("threads_numbers malloc");
        pthread_exit((void*) EXIT_FAILURE);
    }

    int return_value;
    for (int i = 0; i < threads_count; i++) {
        threads_numbers[i] = i;
        return_value = pthread_create(threads + i, NULL, (void*) calculate_pi_part, (void*) &threads_numbers[i]);
        if (0 != return_value) {
            fprintf(stderr, "Cannot create a thread: %s", strerror(return_value));
            pthread_exit((void*) EXIT_FAILURE);
        }
    }

    double pi_div_by_4 = 0;
    double* thread_return_value = NULL;
    for (int i = 0; i < threads_count; i++) {
        return_value = pthread_join(threads[i], (void**) &thread_return_value);
        if (0 != return_value) {
            fprintf(stderr, "Cannot join a thread: %s", strerror(return_value));
            free(thread_return_value);
            pthread_exit((void*) EXIT_FAILURE);
        }
        pi_div_by_4 += *thread_return_value;
        free(thread_return_value);
    }

    printf("\nCalculated Pi = %.15lf\n", pi_div_by_4 * 4);
    printf("      Real Pi = %.15lf\n", M_PI);
    free(threads);
    pthread_exit((void*) EXIT_SUCCESS);
}
