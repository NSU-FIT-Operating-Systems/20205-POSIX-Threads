#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

#define PHILO 5
#define DELAY 30000
#define FOOD 50

pthread_mutex_t forks[PHILO];
pthread_t phils[PHILO];
pthread_mutex_t foodlock;
pthread_mutex_t taking_forks;
FILE* output;

void get_fork(int phil,
			  int fork,
			  char* hand) {
	if (pthread_mutex_lock(&forks[fork])) {
		perror("Picking up fork");
		exit(EXIT_FAILURE);
	}
	fprintf(output, "%d TAKE_ONE %d\n", phil, fork);
}

void down_forks(int id, int f1,
				int f2) {
	if (pthread_mutex_unlock(&forks[f1])) {
		perror("putting down fork");
		exit(EXIT_FAILURE);
	}
	if (pthread_mutex_unlock(&forks[f2])) {
		perror("putting down fork");
		exit(EXIT_FAILURE);
	}
	fprintf(output, "%d PUT %d %d\n", id, f1, f2);
}

int food_on_table() {
	static int food = FOOD;
	int myfood;

	if (pthread_mutex_lock(&foodlock)) {
		perror("locking food");
		exit(EXIT_FAILURE);
	}
	if (food > 0) {
		food--;
	}
	myfood = food;
	if (pthread_mutex_unlock(&foodlock)) {
		perror("unlocking food");
		exit(EXIT_FAILURE);
	}
	return myfood;
}

void* philosopher(void* num) {
	int id;
	int left_fork, right_fork, f;

	id = (int)num;
	right_fork = id;
	left_fork = id + 1;

	if (left_fork == PHILO) {
		left_fork = 0;
	}

	while (f = food_on_table()) {
		if (pthread_mutex_lock(&taking_forks) != 0) {
			fprintf(stderr, "Error locking mutex by phil %d\n", id);
			exit(EXIT_FAILURE);
		}
		get_fork(id, right_fork, "right");
		get_fork(id, left_fork, "left ");
		if (pthread_mutex_unlock(&taking_forks) != 0) {
			fprintf(stderr, "Error unlocking mutex by phil %d\n", id);
			exit(EXIT_FAILURE);
		}
		fprintf(output, "%d EATING\n", id);
		usleep(DELAY * (FOOD - f + 1));
		down_forks(id, left_fork, right_fork);
		fprintf(output, "%d THINKING\n", id);

	}
	printf("Philosopher %d is done eating.\n", id);
	return (NULL);
}

int main(int argn, char** argv) {
	int i;
	output = fopen("output_s.txt", "w");
	if (pthread_mutex_init(&foodlock, NULL) != 0) {
		perror("Can't init foodlock");
		return EXIT_FAILURE;
	}
	if (pthread_mutex_init(&taking_forks, NULL) != 0) {
		perror("Can't init taking_forks");
		return EXIT_FAILURE;
	}
	for (i = 0; i < PHILO; i++) {
		if (pthread_mutex_init(&forks[i], NULL) != 0) {
			perror("Can't init forks");
			return EXIT_FAILURE;
		}
	}
	for (i = 0; i < PHILO; i++) {
		if (pthread_create(&phils[i], NULL, philosopher, (void*)i) != 0) {
			perror("Can't create thread");
			return EXIT_FAILURE;
		}
	}
	for (i = 0; i < PHILO; i++) {
		if (pthread_join(phils[i], NULL) != 0) {
			perror("Can't join thread");
			return EXIT_FAILURE;
		}
	}
	if (pthread_mutex_destroy(&foodlock) != 0) {
		perror("Can't destroy foodlock");
		return EXIT_FAILURE;
	}
	if (pthread_mutex_destroy(&taking_forks) != 0) {
		perror("Can't destroy taking_forks");
		return EXIT_FAILURE;
	}
	for (i = 0; i < PHILO; i++) {
		if (pthread_mutex_destroy(&forks[i]) != 0) {
			perror("Can't destroy forks");
			return EXIT_FAILURE;
		}
	}

	return EXIT_SUCCESS;
}
