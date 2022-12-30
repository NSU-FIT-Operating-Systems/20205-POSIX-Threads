#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <math.h>
#include <unistd.h>
#include <sched.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/syscall.h>
#include "sync_pipe/sync_pipe.h"

#define ERROR_CODE (-1)

long int threads_count;
pthread_t* threads;
bool is_time_to_stop = false;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
struct timespec start_writing_pipe;
int start_fd;
int write_fd;
pthread_mutex_t counter_lock = PTHREAD_MUTEX_INITIALIZER;
int counter = 0;

typedef struct thread_parameters {
    int thread_number;
    double result;
    double time_waiting;
} param_t;

param_t* threads_data;

int pipeWait() {
    char buf;
    ssize_t was_read = read(start_fd, &buf, 1);
    if (was_read < 0) {
        perror("Error read for synchronization");
    }
    return (int) was_read;
}

int syncPipeInit() {
    int pipe_fds[2];
    int pipe_res = pipe(pipe_fds);
    if (pipe_res != 0) {
        perror("Error pipe():");
    }
    start_fd = pipe_fds[0];
    write_fd = pipe_fds[1];
    return pipe_res;
}

void syncPipeClose() {
    close(start_fd);
    close(write_fd);
}

int pipeNotify(int num_really_created_threads) {
    char start_buf[BUFSIZ];
    ssize_t bytes_written = 0;
    while (bytes_written < num_really_created_threads) {
        ssize_t written = 0;
        if (num_really_created_threads - bytes_written <= BUFSIZ) {
            written = write(write_fd, start_buf, num_really_created_threads - bytes_written);
        } else {
            written = write(write_fd, start_buf, BUFSIZ);
        }
        if (written < 0) {
            perror("Error write");
            fprintf(stderr, "bytes_written: %ld / %d\n", bytes_written, num_really_created_threads);
        } else {
            bytes_written += written;
        }
    }
}

static void
display_sched_attr(int policy, struct sched_param* param) {
    printf("    policy=%s, priority=%d\n",
           (policy == SCHED_FIFO) ? "SCHED_FIFO" :
           (policy == SCHED_RR) ? "SCHED_RR" :
           (policy == SCHED_OTHER) ? "SCHED_OTHER" :
           "???",
           param->sched_priority);
}

double* calculate_pi_part(param_t* data) {
    int policy;
    struct sched_param param;

    pthread_getschedparam(pthread_self(), &policy, &param);

    pthread_mutex_lock(&counter_lock);
    printf("Thread %d. ", data->thread_number);
    display_sched_attr(policy, &param);

//    pthread_attr_t attr;
//    pthread_attr_init(&attr);
//    policy = SCHED_RR;
//    pthread_attr_setschedpolicy(&attr, policy);

//    param.sched_priority = 70 * (counter / 50) + 1;
//    counter++;
//    pthread_setschedparam(pthread_self(), policy, &param);

//    printf("Thread %d. ", data->thread_number);
//    display_sched_attr(policy, &param);
//    printf("\n");
    pthread_mutex_unlock(&counter_lock);

    int return_value;
    long i;
    for (i = data->thread_number; i * 4 + 3 <= LONG_MAX; i += threads_count) {
        if (((i - data->thread_number) / threads_count) % 10000000 == 0 && i != data->thread_number) {
            return_value = pthread_mutex_lock(&lock);
            if (return_value != 0) {
                fprintf(stderr, "mutex_lock: %s", strerror(return_value));
                return (double*) ERROR_CODE;
            }

            if (is_time_to_stop) {
                return_value = pthread_mutex_unlock(&lock);
                printf("Thread %d: %ld\n", data->thread_number, (i - data->thread_number) / threads_count);

                if (return_value != 0) {
                    fprintf(stderr, "mutex_unlock: %s", strerror(return_value));
                    return (double*) ERROR_CODE;
                }
                return (double*) EXIT_SUCCESS;
            }
            return_value = pthread_mutex_unlock(&lock);
            if (return_value != 0) {
                fprintf(stderr, "mutex_unlock: %s", strerror(return_value));
                return (double*) ERROR_CODE;
            }
        }
        data->result += 1.0 / (double) (i * 4 + 1);
        data->result -= 1.0 / (double) (i * 4 + 3);
//        sched_yield();
    }
    printf("Thread %d: %ld\n", data->thread_number, (i - data->thread_number) / threads_count);
    return (double*) EXIT_SUCCESS;
}

void makeCleanup() {
    free(threads_data);
    free(threads);
    syncPipeClose();
    pthread_mutex_destroy(&lock);
    pthread_mutex_destroy(&counter_lock);
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

    sigset_t oldmask;
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    int return_value;
    if ((return_value = pthread_sigmask(SIG_BLOCK, &mask, &oldmask)) != 0) {
        fprintf(stderr, "pthread_sigmask SIG_BLOCK: %s", strerror(return_value));
        pthread_exit((void*) EXIT_FAILURE);
    }

    threads = (pthread_t*) malloc(threads_count * sizeof(pthread_t));
    if (threads == NULL) {
        perror("threads malloc");
        pthread_exit((void*) EXIT_FAILURE);
    }

    threads_data = (param_t*) malloc(threads_count * sizeof(param_t));
    if (threads_data == NULL) {
        perror("threads_data malloc");
        free(threads);
        pthread_exit((void*) EXIT_FAILURE);
    }

    syncPipeInit();

    return_value = atexit(makeCleanup);
    if (return_value != 0) {
        fprintf(stderr, "cannot set exit function\n");
        makeCleanup();
        pthread_exit((void*) EXIT_FAILURE);
    }

    for (int i = 0; i < threads_count; i++) {
        threads_data[i].thread_number = i;
        return_value = pthread_create(threads + i, NULL, (void* (*)(void*)) calculate_pi_part,
                                      (void*) &threads_data[i]);
        if (0 != return_value) {
            fprintf(stderr, "Cannot create a thread: %s", strerror(return_value));
            pthread_exit((void*) EXIT_FAILURE);
        }
    }

//    pipeNotify((int) threads_count);

    int sig_number;
    while (true) {
        return_value = sigwait(&mask, &sig_number);
        if (return_value != 0) {
            perror("sigwait");
        }
        if (sig_number == SIGINT) {
            return_value = pthread_mutex_lock(&lock);
            if (return_value != 0) {
                fprintf(stderr, "mutex_lock: %s", strerror(return_value));
                pthread_exit((void*) EXIT_FAILURE);
            }
            is_time_to_stop = true;
            return_value = pthread_mutex_unlock(&lock);

            if (return_value != 0) {
                fprintf(stderr, "mutex_unlock: %s", strerror(return_value));
                pthread_exit((void*) EXIT_FAILURE);
            }
            break;
        }
    }

    double pi_div_by_4 = 0;
    double thread_return_value;

    bool error = false;
    for (int i = 0; i < threads_count; i++) {
        return_value = pthread_join(threads[i], (void*) &thread_return_value);
        if (0 != return_value) {
            fprintf(stderr, "Cannot join a thread: %s", strerror(return_value));
            error = true;

        } else if (thread_return_value == ERROR_CODE) {
            error = true;
        } else {

            pi_div_by_4 += threads_data[i].result;
        }
    }

    if (error) {
        pthread_exit((void*) EXIT_FAILURE);
    }

    printf("\nCalculated Pi = %.15lf\n", pi_div_by_4 * 4);
    printf("      Real Pi = %.15lf\n", M_PI);

    pthread_exit((void*) EXIT_SUCCESS);
}