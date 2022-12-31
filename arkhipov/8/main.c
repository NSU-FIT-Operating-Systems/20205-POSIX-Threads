#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <alloca.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <pthread.h>

#define NUM_ITERS (1000000000ll)

typedef struct {
    long long int idx;
    long long int size;
    double res;
} Batch;

int running = 1;

int parse_int(char* str) {
    char* endpoint;
    int res = (int)strtol(str, &endpoint, 10);

    if (*endpoint != '\0') {
        fprintf(stderr, "Expect lines count, got: %s\n", str);
        exit(1);
    }
    return res;
}

double calculate_part(long long int idx) {
    if (idx % 2 == 0) {
        return 1.0 / (idx * 2 + 1);
    } else {
        return -1.0 / (idx * 2 + 1);
    }
}

void* calculate_batch(void* args) {
    Batch* batch = (Batch*)(args);
    long long int l = batch->idx;
    long long int r = l + batch->size;

    double res = 0.0;
    for (long long int i = l; i < r && running; ++i) {
        res += calculate_part(i);
    }

    batch->res = res;
    pthread_exit(NULL);
}


int main(int argc, char* argv[]) {
    
    if (argc != 2) {
        fprintf(stderr, "Specify exactly 1 argument: number of threads\n");
        return 1;
    }
	
    int threads_num = parse_int(argv[1]);

    struct rlimit mem_limit;
    if (getrlimit(RLIMIT_STACK, &mem_limit) != 0) {
        fprintf(stderr, "Error: Unable to check memory limit\n");
        return 1;
    }
    unsigned long req_size = 1lu * threads_num * (sizeof(pthread_t) + sizeof(Batch));
    unsigned long lim_size = (unsigned long)(mem_limit.rlim_cur);

    if (req_size > lim_size) {
        fprintf(stderr, "Error: Unable to allocate. limit: %lu, require: %lu.\n", lim_size, req_size);
        return 1;
    }

    pthread_t threads[threads_num]; 
    Batch threads_args[threads_num];
    
    long long int batch_default_size = NUM_ITERS / (1ll * threads_num);
    long long int max_append_thread = NUM_ITERS % (1ll * threads_num);
    long long int shift = 0ll;

    for (int i = 0; i < threads_num; ++i) {
	    threads_args[i].idx = shift;
	    threads_args[i].size = batch_default_size;
	    if (i < max_append_thread) {
            threads_args[i].size++;
	    }
        shift += threads_args[i].size;
	
	    Batch* arg = &(threads_args[i]);
        int err = pthread_create(&(threads[i]), NULL, calculate_batch, (void*)(arg));

	    if (err != 0) {
            fprintf(stderr, "Error while creating thread %d: %s\n", i, strerror(err));
            running = 0;
            pthread_exit(NULL);
	    }
    }

    double threads_res = 0.0;
    for (int i = 0; i < threads_num; ++i) {
        int err = pthread_join(threads[i], NULL);
        if (err != 0) {
    	    fprintf(stderr, "Error while joining thread %d: %s\n", i, strerror(err));
	        pthread_exit(NULL);
	    }
        threads_res += threads_args[i].res;
    }
    threads_res *= 4;

    printf("calculated pi: %.10lf\n", threads_res);
    printf("correct    pi: %.10lf\n", M_PI);

    pthread_exit(NULL);
}
