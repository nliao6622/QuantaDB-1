#include <x86intrin.h>
#include <stdio.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <sched.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/time.h>
#include <errno.h>
#include <string>
#include <atomic>
#include <iostream>

/*
 * Say, TSC(t) denotes TSC counter value generated at time 't'.
 * Say, real time t1, t2 where t2 > t1.
 * We expect TSC(t2) should always be greater than TSC(t1). 
 *
 * This program verifies TSC generated by the __rdtscp() function does meet the above expectation.
 * This program proofs   TSC generated by the __rdtsc()  function failed the about expectation.
 */

#define MAX_CORE 32

static inline u_int64_t rdtscp(u_int32_t &aux)
{
    u_int64_t rax,rdx;
    asm volatile ( "rdtscp\n" : "=a" (rax), "=d" (rdx), "=c" (aux) : : );
    return (rdx << 32) + rax;
}

int thread_run_run = 1;
std::atomic<uint64_t> tscbuf[MAX_CORE];

void *func(void *arg)
{
    u_int32_t core;
    uint32_t id = (uint64_t)arg & 0xFFFFFFFF;;
    int call_rdtsc = ((uint64_t)arg >> 32);

    printf("thread %d starting on core %d ...\n", id, sched_getcpu());

    while (thread_run_run) {
        uint32_t cmp_id = (id > 0)? id - 1 : MAX_CORE - 1;
        uint64_t tsc_cmp = tscbuf[cmp_id];
        uint64_t tsc = (call_rdtsc != 0)? __rdtsc() : __rdtscp(&core);
        if (tsc < tsc_cmp) {
            printf("Error: tsc[%d] %ld, tsc[%d] %ld diff %ld\n", id, tsc, cmp_id, tsc_cmp, tsc_cmp - tsc); 
        }
        tscbuf[id] = tsc;
    }

}

int main(int ac, char *av[])
{
    if ((ac < 2) || !(strcmp(av[1], "rdtsc") == 0 || strcmp(av[1], "rdtscp") == 0)) {
        printf("Usage: %s <rdtsc|rdtscp> [<runtime in sec>]\n", av[0]);
        exit(1);
    }

    int runtime = (ac < 3)? 5 : atoi(av[2]);
    bool call_rdtsc = (strcmp(av[1], "rdtsc") == 0); 
        
    pthread_t threads[MAX_CORE];
    for (int i=0; i<MAX_CORE; i++) {
        uint64_t arg = ((uint64_t)call_rdtsc << 32) + i;
        pthread_create(&threads[i], NULL, func, (void*)arg);
    }

    sleep(runtime);

    thread_run_run = 0;

    for (int i=0; i<MAX_CORE; i++) {
        pthread_join(threads[i], NULL);
    }

    return 0;
}
