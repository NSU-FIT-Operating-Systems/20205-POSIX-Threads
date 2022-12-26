#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stdbool.h>
#include "math.h"
#include <time.h>
#define ll long long int

bool do_i_need_to_die = false;
long number_of_threads;
pthread_mutex_t max_iteration_completed_mutex;
ll max_iteration_completed = 0;

typedef struct {
	ll iterate_from;
} ThreadArgs;

typedef struct {
	double partial_sum;
	ll number_of_iterations;
} ThreadResult;

long long int supplement;

void* calc_partial_sum(void* args) {
	ThreadResult * result = (ThreadResult *)malloc(sizeof(ThreadResult));
	double partial_sum = 0;
	ll i;
	for (i = ((ThreadArgs*)args)->iterate_from; ; i += number_of_threads) {
		partial_sum += 1.0 / (i * 4.0 + 1.0);
		partial_sum -= 1.0 / (i * 4.0 + 3.0);
		if (i >= max_iteration_completed + supplement) {
			pthread_mutex_lock(&max_iteration_completed_mutex);
			if (i >= max_iteration_completed + supplement) {
				max_iteration_completed = i;
				pthread_mutex_unlock(&max_iteration_completed_mutex);
			}
			else {
				pthread_mutex_unlock(&max_iteration_completed_mutex);
				continue;
			}
			if (do_i_need_to_die) {
				result->partial_sum = partial_sum;
				result->number_of_iterations = i - ((ThreadArgs*)args)->iterate_from;
				pthread_exit((void*)result);
			}
		}
	}
}

void signal_catcher(int sig) {
	if (sig == SIGINT) {
		do_i_need_to_die = true;
	}
}

int main(int argc, char** argv) {
	int err;
	if (argc < 3) {
		fprintf(stderr, "Usage: %s <number of threads> <supplement> \n", argv[0]);
		return EXIT_FAILURE;
	}
	char* endptr;
	number_of_threads = strtol(argv[1], &endptr, 10);;
	if (number_of_threads < 1) {
		fprintf(stderr, "Number of threads must be greater than 0\n");
		return EXIT_FAILURE;
	}
	supplement = strtol(argv[2], &endptr, 10);
	struct sigaction act;
	act.sa_handler = signal_catcher;
	sigaddset(&act.sa_mask, SIGINT);
	if (sigaction(SIGINT, &act, NULL) == -1) {
		perror("Failed to change signal SIGINT handler function");
		exit(EXIT_FAILURE);
	}

	ThreadArgs args[number_of_threads];
	pthread_t threads[number_of_threads];

	for (int i = 0; i < number_of_threads; ++i) {
		args[i].iterate_from = i;
	}

	time_t start,end;
	time(&start);
	for (int i = 0; i < number_of_threads; ++i) {
		err = pthread_create(&threads[i], NULL, calc_partial_sum, &args[i]);
		if (err != 0) {
			fprintf(stderr, "Error creating thread %s", strerror(err));
		}
	}

	double pi = 0;
	ll min_number_of_iterations = 9223372036854775807, max_number_of_iterations = 0;
	for (int i = 0; i < number_of_threads; ++i) {
		void* result;
		err = pthread_join(threads[i], &result);
		if (err != 0) {
			fprintf(stderr, "Error joining thread %s", strerror(err));
		}
		pi += ((ThreadResult*)result)->partial_sum;
		if (((ThreadResult*)result)->number_of_iterations < min_number_of_iterations) {
			min_number_of_iterations = ((ThreadResult*)result)->number_of_iterations;
		}
		if (((ThreadResult*)result)->number_of_iterations > max_number_of_iterations) {
			max_number_of_iterations = ((ThreadResult*)result)->number_of_iterations;
		}
		free(result);
	}
	time(&end);
	printf("calculated pi = %.15g \n", pi * 4.0);
	printf("pi = %.15g \n", M_PI);
	printf("min number of iterations = %lld \n", min_number_of_iterations);
	printf("max number of iterations = %lld \n", max_number_of_iterations);
	printf("difference = %f %% \n", (double)((double)(max_number_of_iterations
		- min_number_of_iterations) * 100/ (double)max_number_of_iterations));
	return 0;
}


