/*
 * Small benchmark utility for memorypool
 *
 * Author: fmontorsi
 * Created: Feb 2019
 * License: BSD license
 *
 */

//------------------------------------------------------------------------------
// Includes
//------------------------------------------------------------------------------

#include <errno.h>
#include <map>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <unistd.h>

#include "boost_intrusive_pool.hpp"
#include "json-lib.h"
#include "performance_timing.h"

using namespace memorypool;

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

/* Benchmark duration in seconds.  */
#define BENCHMARK_DURATION 60
#define RAND_SEED 88

//------------------------------------------------------------------------------
// Types for benchmarks
//------------------------------------------------------------------------------

class LargeObject : public memorypool::boost_intrusive_pool_item {
public:
    LargeObject() {}
    ~LargeObject() {}

    virtual char dummy() const { return buf[0]; }
    void init(uint32_t n = 0) { buf[0] = 'a' + (n & 0x11); }

private:
    // just some fat buffer:
    char buf[1024];
};

typedef boost::intrusive_ptr<LargeObject> HLargeObject;

class NoPool {
public:
    NoPool() {}

    HLargeObject allocate_through_ctor() { return HLargeObject(new LargeObject); }
};

//------------------------------------------------------------------------------
// Benchmarks
//------------------------------------------------------------------------------

template <class PoolUnderTest>
static void main_benchmark_loop(PoolUnderTest& pool, size_t num_elements, size_t& num_freed, size_t& max_active)
{
    num_freed = 0, max_active = 0;
    std::map<int, HLargeObject> helper_container;
    for (unsigned int i = 0; i < num_elements; i++) {
        HLargeObject myInt = pool.allocate_through_ctor();
        assert(myInt);

        helper_container[i] = myInt;

        // returns to the factory a few items in pseudo-random order
        if ((i % 7) == 0 || (i % 53) == 0 || (i % 12345) == 0) {
            size_t value_to_release = i / 10;

            auto it = helper_container.find(value_to_release);
            if (it != helper_container.end()) {
                // erasing the smart pointer from the std::map will trigger its return to the memory pool:
                helper_container.erase(value_to_release);
                num_freed++;
            }
        }

        max_active = std::max(max_active, helper_container.size());
    }
}

static void do_benchmark(json_ctx_t& json_ctx)
{
    timing_t start, stop, elapsed[2] = { 0 };

    struct {
        unsigned int initial_size;
        unsigned int enlarge_step;
        unsigned int num_items;
    } testArray[] = {
        { 128, 64, (int)1e6 }, // force newline
        { 1024, 128, (int)1e6 }, // force newline
        { 1024 * 1024, 128, (int)1e6 }, // force newline
        { 1024 * 1024, 128, (int)1e7 }, // force newline
    };

    for (int i = 0; i < sizeof(testArray) / sizeof(testArray[0]); i++) {
        size_t num_freed[2], max_active[2];

        // run the benchmark with boost_intrusive_pool
        boost_intrusive_pool<LargeObject> real_pool(
            testArray[i].initial_size /* initial size */, testArray[i].enlarge_step /* enlarge step */);
        TIMING_NOW(start);
        main_benchmark_loop(real_pool, testArray[i].num_items, num_freed[0], max_active[0]);
        TIMING_NOW(stop);
        TIMING_DIFF(elapsed[0], start, stop);

        // run the benchmark with NoPool
        NoPool comparison_pool;
        TIMING_NOW(start);
        main_benchmark_loop(comparison_pool, testArray[i].num_items, num_freed[1], max_active[1]);
        TIMING_NOW(stop);
        TIMING_DIFF(elapsed[1], start, stop);

        // output results as JSON:

        json_attr_object_begin(&json_ctx, (std::string("run_") + std::to_string(i)).c_str());

        struct rusage usage;
        getrusage(RUSAGE_SELF, &usage);

        // test setup
        json_attr_double(&json_ctx, "initial_size", testArray[i].initial_size);
        json_attr_double(&json_ctx, "enlarge_step", testArray[i].enlarge_step);
        json_attr_double(&json_ctx, "num_items", testArray[i].num_items);

        // test results
        json_attr_object_begin(&json_ctx, "boost_intrusive_pool_item");
        json_attr_double(&json_ctx, "duration_nsec", elapsed[0]);
        json_attr_double(&json_ctx, "duration_nsec_per_item", (double)elapsed[0] / (double)testArray[i].num_items);
        json_attr_double(&json_ctx, "num_items_freed", num_freed[0]);
        json_attr_double(&json_ctx, "max_active_items", max_active[0]);
        json_attr_double(&json_ctx, "max_rss", usage.ru_maxrss);
        json_attr_object_end(&json_ctx); // boost_intrusive_pool_item

        // test results
        json_attr_object_begin(&json_ctx, "plain_malloc");
        json_attr_double(&json_ctx, "duration_nsec", elapsed[1]);
        json_attr_double(&json_ctx, "duration_nsec_per_item", (double)elapsed[1] / (double)testArray[i].num_items);
        json_attr_double(&json_ctx, "num_items_freed", num_freed[1]);
        json_attr_double(&json_ctx, "max_active_items", max_active[1]);
        json_attr_double(&json_ctx, "max_rss", usage.ru_maxrss);
        json_attr_object_end(&json_ctx); // plain_malloc

        json_attr_object_end(&json_ctx); // run
    }
}

static void do_json_benchmark()
{
    size_t iters = 0, num_threads = 1;
    json_ctx_t json_ctx;
    unsigned long res;

    json_init(&json_ctx, 0, stdout);

    json_document_begin(&json_ctx);
    json_attr_string(&json_ctx, "timing_type", TIMING_TYPE);
    json_attr_object_begin(&json_ctx, "memory_pool");

    TIMING_INIT(res);

    do_benchmark(json_ctx);

    json_attr_object_end(&json_ctx);
    json_document_end(&json_ctx);
    printf("\n");
}

static void usage(const char* name)
{
    fprintf(stderr, "%s: <num_threads>\n", name);
    exit(1);
}

int main(int argc, char** argv)
{
    if (argc > 1)
        usage(argv[0]);

    do_json_benchmark();

    return 0;
}
