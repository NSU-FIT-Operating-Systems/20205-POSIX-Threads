#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>

#define PHILO 5
#define DELAY 30000
#define FOOD 50
enum { THINKING, HUNGRY, EATING } state[PHILO];
pthread_t phils[PHILO];
pthread_mutex_t foodlock;
FILE* output;
sem_t sem;
sem_t S[PHILO];
char* colors[5] = {"\x1B[31m", "\x1B[32m", "\x1B[33m", "\x1B[34m", "\x1B[35m"};

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

void test(int id) {
	if (state[id] == HUNGRY && state[(id + 1) % PHILO] != EATING && state[(id + PHILO - 1) % PHILO] != EATING) {
		state[id] = EATING;
		fprintf(output, "%d TAKE %d %d\n", id, id % PHILO, (id + 1) % PHILO);
		sem_post(&S[id]);
	}
}

void get_forks(int id) {
	sem_wait(&sem);
	state[id] = HUNGRY;
	fprintf(output, "%d HUNGRY\n", id);
	test(id);
	sem_post(&sem);
	sem_wait(&S[id]);
}

void put_forks(int id) {
	sem_wait(&sem);
	state[id] = THINKING;
	fprintf(output, "%d PUT %d %d\n", id, id % PHILO, (id + 1) % PHILO);
	fprintf(output, "%d THINKING\n", id);
	test((id + 1) % PHILO);
	test((id + PHILO - 1) % PHILO);
	sem_post(&sem);
}

void* philosopher(void* num) {
	int id;
	int f;

	id = (int)num;
	while (f = food_on_table()) {
		get_forks(id);
		fprintf(output, "%d EATING\n", id);
		usleep(DELAY * (FOOD - f + 1));
		put_forks(id);
	}
	return (NULL);
}

int main(int argn, char** argv) {
	int i;

	output = fopen("output_f.txt", "w");
	if (pthread_mutex_init(&foodlock, NULL) != 0) {
		perror("Can't init foodlock");
		return EXIT_FAILURE;
	}
	if (sem_init(&sem, 0, 1) != 0) {
		perror("Can't init semaphore");
		return EXIT_FAILURE;
	}
	for (i = 0; i < PHILO; i++) {
		if (sem_init(&S[i], 0, 0) != 0) {
			perror("Can't init semaphore");
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
	if (sem_destroy(&sem) != 0) {
		perror("Can't destroy semaphore");
		return EXIT_FAILURE;
	}
	for (i = 0; i < PHILO; i++) {
		if (sem_destroy(&S[i]) != 0) {
			perror("Can't destroy semaphore");
			return EXIT_FAILURE;
		}
	}
	return EXIT_SUCCESS;
}