#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <semaphore.h>

#define PHILO 5
#define DELAY 30000
#define FOOD 500


int ph_state[PHILO];
pthread_mutex_t forks[PHILO];
pthread_t phils[PHILO];

void *philosopher (void *id);
int food_on_table ();
void get_fork (int, int, char*);
void down_forks (int, int);

pthread_mutex_t foodlock;
sem_t waiter;

int sleep_seconds = 0;


void* philosopher (void *num) {
  int counter = 0;
  int* _id = (int*)num;
  int id = *_id;
  int left_fork, right_fork, f;

  printf ("Ph %d start.\n", id);
  
  right_fork = id;
  left_fork = id + 1;
  
  if (left_fork == PHILO)
    left_fork = 0;

  f = food_on_table(); 
  while (f) {
    if (id == 1)
      sleep (sleep_seconds);

    printf ("Ph %d: get dish %d.\n", id, f);
    sem_wait(&waiter);
    get_fork (id, left_fork, "l");
    get_fork (id, right_fork, "r");

    printf ("Ph %d: eating.\n", id);
    usleep (DELAY * (FOOD - f + 1));
    counter++;
    down_forks (left_fork, right_fork);

    f = food_on_table();
  }
  printf ("Ph %d is done. eated: %d\n", id, counter);
  return (NULL);
}


int food_on_table () {
  static int food = FOOD;
  int myfood;

  pthread_mutex_lock (&foodlock);
  
  if (food > 0) {
    food--;
  }
  myfood = food;
  
  pthread_mutex_unlock (&foodlock);
  return myfood;
}


void get_fork (int phil, int fork, char* hand) {
  pthread_mutex_lock (&forks[fork]);
  printf ("Ph %d: got %s fork %d\n", phil, hand, fork);
}


void down_forks (int f1, int f2) {
  pthread_mutex_unlock (&forks[f1]);
  printf("released %d\n", f1);

  pthread_mutex_unlock (&forks[f2]);
  printf("released %d\n", f2);

  sem_post(&waiter);
}


int main (int argn, char **argv) {
  int i;

  if (argn == 2)
    sleep_seconds = atoi (argv[1]);

  sem_init(&waiter, 0, PHILO - 1);

  pthread_mutex_init (&foodlock, NULL);
  for (i = 0; i < PHILO; i++)
    pthread_mutex_init (&forks[i], NULL);

  int ph_ids[PHILO];
  for (i = 0; i < PHILO; i++) ph_ids[i] = i; 
  for (i = 0; i < PHILO; i++)
    pthread_create (&phils[i], NULL, philosopher, (void *)(&ph_ids[i]));
  for (i = 0; i < PHILO; i++)
    pthread_join (phils[i], NULL);

  sem_destroy(&waiter);
  return 0;
}


