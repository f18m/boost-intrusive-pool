#pragma once

#include <time.h>
typedef uint64_t timing_t;

#define TIMING_TYPE "clock_gettime"

/* Measure the resolution of the clock so we can scale the number of
   benchmark iterations by this value.  */
#define TIMING_INIT(res)                                                                                               \
    ({                                                                                                                 \
        struct timespec start;                                                                                         \
        clock_getres(CLOCK_PROCESS_CPUTIME_ID, &start);                                                                \
        (res) = start.tv_nsec;                                                                                         \
    })

#define TIMING_NOW(var)                                                                                                \
    ({                                                                                                                 \
        struct timespec tv;                                                                                            \
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &tv);                                                                  \
        (var) = (uint64_t)(tv.tv_nsec + (uint64_t)1000000000 * tv.tv_sec);                                             \
    })

#define TIMING_DIFF(diff, start, end) (diff) = (end) - (start)
#define TIMING_ACCUM(sum, diff) (sum) += (diff)

#define TIMING_PRINT_MEAN(d_total_s, d_iters) printf("\t%g", (d_total_s) / (d_iters))
