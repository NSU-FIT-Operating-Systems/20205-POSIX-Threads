#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct {
	int iterate_from;
	int iterate_to;
} ThreadArgs;

void* calc_partial_sum(void* args) {
	double* partial_sum = (double*)malloc(sizeof(double));
	for (int i = ((ThreadArgs*)args)->iterate_from; i < ((ThreadArgs*)args)->iterate_to; ++i) {
		*partial_sum += 1.0 / (i * 4.0 + 1.0);
		*partial_sum -= 1.0 / (i * 4.0 + 3.0);
	}
	pthread_exit((void*)partial_sum);
}

int main(int argc, char** argv) {
	int err;
	if (argc < 3) {
		fprintf(stderr, "Usage: %s <number of threads> <number of iterations>\n", argv[0]);
		return EXIT_FAILURE;
	}
	if (atoi(argv[1]) < 1) {
		fprintf(stderr, "Number of threads must be greater than 0\n");
		return EXIT_FAILURE;
	}
	if (atoi(argv[2]) < 1) {
		fprintf(stderr, "Number of iterations must be greater than 0\n");
		return EXIT_FAILURE;
	}
	int number_of_threads = atoi(argv[1]);
	FILE* max_pid_file = fopen("/proc/sys/kernel/pid_max", "r");
	int max_pid;
	if(fscanf(max_pid_file, "%d", &max_pid) != 1) {
		fprintf(stderr, "Error reading max pid\n");
		return EXIT_FAILURE;
	}
	if (number_of_threads > max_pid) {
		number_of_threads = max_pid - 100;
		fprintf(stderr, "Number of threads was set to %d, because you wanted too much\n", number_of_threads);
	}
	ThreadArgs args[number_of_threads];
	pthread_t threads[number_of_threads];
	int number_of_iterations = atoi(argv[2]) + 1;
	int last_iteration_index = 0;
	for (int i = 0; i < number_of_threads; ++i) {
		args[i].iterate_from = last_iteration_index;
		args[i].iterate_to = (number_of_iterations - 1) / number_of_threads
			+ (i < (number_of_iterations - 1) % number_of_threads) + last_iteration_index;
		last_iteration_index = args[i].iterate_to;
		err = pthread_create(&threads[i], NULL, calc_partial_sum, &args[i]);
		if (err != 0) {
			fprintf(stderr, "Error creating thread %s", strerror(err));
		}
	}

	double pi = 0;
	for (int i = 0; i < number_of_threads; ++i) {
		void* partial_sum;
		err = pthread_join(threads[i], &partial_sum);
		if (err != 0) {
			fprintf(stderr, "Error joining thread %s", strerror(err));
		}
		pi += *(double*)partial_sum;
		free(partial_sum);
	}
	printf("calculated pi = %.15g \n", pi * 4.0);
	printf("pi = %.15g \n", M_PI);
	pthread_exit(NULL);
}
