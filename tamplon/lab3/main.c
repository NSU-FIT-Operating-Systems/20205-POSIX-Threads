#include <stdio.h>
#include <pthread.h>
#include <string.h>

struct ThreadArgs {
	int start;
	int number_of_elements_to_print;
	char** argv;
};
typedef struct ThreadArgs ThreadArgs;

void* PrintSampleText(void* args) {
	for (int i = ((ThreadArgs*)args)->start;
		 i < ((ThreadArgs*)args)->start + ((ThreadArgs*)args)->number_of_elements_to_print; ++i) {
		printf("%d %s\n", i, ((ThreadArgs*)args)->argv[i]);
	}
	pthread_exit(NULL);
}

int main(int argc, char** argv) {
	int err;
	if (argc < 5) {
		printf("Too few arguments\n");
		return 0;
	}
	const int NUMBER_OF_THREADS = 4;
	pthread_t threads[NUMBER_OF_THREADS];
	ThreadArgs args[NUMBER_OF_THREADS];
	int last_word_printed_index = 1;
	for (int i = 0; i < NUMBER_OF_THREADS; ++i) {
		args[i].number_of_elements_to_print = (argc - 1) / NUMBER_OF_THREADS + (i < (argc - 1) % NUMBER_OF_THREADS);
		args[i].start = last_word_printed_index;
		args[i].argv = argv;
		err = pthread_create(&threads[i], NULL, PrintSampleText, &args[i]);
		if (err != 0) {
			printf("Cannot create a thread %s\n", strerror(err));
			return 1;
		}
		last_word_printed_index += args[i].number_of_elements_to_print;
	}

	for (int i = 0; i < 4; ++i) {
		err = pthread_join(threads[i], NULL);
		if (err != 0) {
			printf("Cannot join a thread %s\n", strerror(err));
			return 1;
		}
	}
	pthread_exit(NULL);
}