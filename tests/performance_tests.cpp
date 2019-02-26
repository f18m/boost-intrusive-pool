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

#include <map>
#include <sys/resource.h>
#include <sys/time.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

#include "boost_intrusive_pool.hpp"
#include "json-lib.h"
#include "performance_timing.h"

using namespace memorypool;

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

#define NUM_AVERAGING_RUNS (10)

typedef enum {
    BENCH_PATTERN_CONTINUOUS_ALLOCATION,
    BENCH_PATTERN_MIXED_ALLOC_FREE,
} BenchPattern_t;

std::string BenchPattern2String(BenchPattern_t t)
{
    switch (t) {
    case BENCH_PATTERN_CONTINUOUS_ALLOCATION:
        return "Continuous allocations, bulk free at end";
    case BENCH_PATTERN_MIXED_ALLOC_FREE:
        return "Mixed alloc/free pattern";
    default:
        return "";
    }
}

//------------------------------------------------------------------------------
// MemoryPooled items for benchmark testing:
//------------------------------------------------------------------------------

class LargeObject : public memorypool::boost_intrusive_pool_item {
public:
    LargeObject() { m_ctor_count++; }
    ~LargeObject() { m_dtor_count++; }

    virtual char dummy() const { return buf[0]; }
    void init(uint32_t n = 0) { buf[0] = 'a' + (n & 0x11); }

    static void reset_counts()
    {
        LargeObject::m_ctor_count = 0;
        LargeObject::m_dtor_count = 0;
    }

private:
    // just some fat buffer:
    char buf[1024];

public:
    static unsigned long m_ctor_count;
    static unsigned long m_dtor_count;
};

typedef boost::intrusive_ptr<LargeObject> HLargeObject;

unsigned long LargeObject::m_ctor_count = 0;
unsigned long LargeObject::m_dtor_count = 0;

//------------------------------------------------------------------------------
// Reference memory pool:
//------------------------------------------------------------------------------

class NoPool {
public:
    NoPool() {}

    // just malloc using new() and run the default ctor:
    HLargeObject allocate_through_ctor() { return HLargeObject(new LargeObject()); }
};

//------------------------------------------------------------------------------
// Benchmarks
//------------------------------------------------------------------------------

template <class PoolUnderTest>
static void main_benchmark_loop(
    PoolUnderTest& pool, BenchPattern_t pattern, size_t num_elements, size_t& num_freed, size_t& max_active)
{
    num_freed = 0, max_active = 0;

    switch (pattern) {
    case BENCH_PATTERN_CONTINUOUS_ALLOCATION: {
        std::vector<HLargeObject> helper_container;
        helper_container.reserve(num_elements);
        for (unsigned int i = 0; i < num_elements; i++) {
            HLargeObject myInt = pool.allocate_through_ctor();
            assert(myInt);

            helper_container.push_back(myInt);
            max_active = std::max(max_active, helper_container.size());
        }

        helper_container.clear();
    } break;

    case BENCH_PATTERN_MIXED_ALLOC_FREE: {
        std::unordered_map<int, HLargeObject> helper_container;
        helper_container.reserve(num_elements / 10);

        for (unsigned int i = 0; i < num_elements; i++) {
            HLargeObject myInt = pool.allocate_through_ctor();
            assert(myInt);

            if ((i % 33) == 0) {
                // we suddenly realize that we don't really need the just-allocated item... release it immediately
                // (it's enough to simply _not_ store it)
            } else {
                helper_container[i] = myInt;

                // returns to the factory a few items in pseudo-random order
                if ((i % 7) == 0 || (i % 31) == 0 || (i % 40) == 0 || (i % 53) == 0) {
                    size_t value_to_release = i - 1;

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
    } break;
    }
}

static void do_benchmark(json_ctx_t* json_ctx)
{
    typedef struct {
        unsigned int initial_size;
        unsigned int enlarge_step;
        unsigned int num_items;
    } config_t;

    typedef struct {
        BenchPattern_t pattern;
        size_t num_configs;
        config_t config[10]; // max num configs
    } pattern_test_t;

    pattern_test_t testPatterns[] = {
        {
            BENCH_PATTERN_CONTINUOUS_ALLOCATION, // force newline
            4, // force newline

            {
                // run #1 dummy test: a lot of objects and memory pool continuously resizing itself: no gain obtained:
                { 1, 1, (int)1e5 },

                // run #2 slightly better situation: the memory pool has to do 64x less resizings:
                { 128, 64, (int)1e5 }, // force newline

                // run #3 much better real-life example: the memory pool starts small but does only a few resizings
                //        because every time it increases by 1024 items:
                { 1024, 1024, (int)1e5 }, // force newline

                // run #4 optimal example: the memory pool starts bigger and does just 7 resizing steps
                { 16384, 16384, (int)1e5 }, // force newline
            } // force newline
        },

        // another more realistic (???) malloc pattern

        {
            BENCH_PATTERN_MIXED_ALLOC_FREE, // force newline
            3, // force newline

            {
                // run #5 in this test the memory pool begins small and does several resizings
                { 1024, 64, (int)1e5 }, // force newline

                // run #6 in this test the memory pool begins small but does less resizings
                { 1024, 128, (int)1e5 }, // force newline

                // run #7 in this test the memory pool begins with already a lot of items, so it does close-to-zero
                //        resizings:
                { 512 * 1024, 1024, (int)1e6 }, // force newline
            } // force newline
        }
    };

    for (int j = 0; j < sizeof(testPatterns) / sizeof(testPatterns[0]); j++) {
        if (json_ctx) {
            json_attr_object_begin(json_ctx, (std::string("pattern_") + std::to_string(j + 1)).c_str());
            json_attr_string(json_ctx, "desc", BenchPattern2String(testPatterns[j].pattern).c_str());
        }

        for (int i = 0; i < testPatterns[j].num_configs; i++) {
            const config_t& runConfig = testPatterns[j].config[i];

            size_t num_freed[2], max_active[2], ctor_count[2], dtor_count[2], num_resizings;
            struct rusage usage[2];
            timing_t avg_time[2];

            // run the benchmark with boost_intrusive_pool
            {
                LargeObject::reset_counts();

                boost_intrusive_pool<LargeObject> real_pool(
                    runConfig.initial_size /* initial size */, runConfig.enlarge_step /* enlarge step */);

                timing_t start, stop, elapsed, accumulated = 0;
                for (int k = 0; k < NUM_AVERAGING_RUNS; k++) {
                    TIMING_NOW(start);
                    main_benchmark_loop(
                        real_pool, testPatterns[j].pattern, runConfig.num_items, num_freed[0], max_active[0]);
                    TIMING_NOW(stop);
                    TIMING_DIFF(elapsed, start, stop);
                    TIMING_ACCUM(accumulated, elapsed);
                }

                avg_time[0] = accumulated / NUM_AVERAGING_RUNS;
                ctor_count[0] = LargeObject::m_ctor_count;
                dtor_count[0] = LargeObject::m_dtor_count;
                num_resizings = real_pool.enlarge_steps_done();

                getrusage(RUSAGE_SELF, &usage[0]);
            }

            // run the benchmark with NoPool
            {
                LargeObject::reset_counts();

                NoPool comparison_pool;

                timing_t start, stop, elapsed, accumulated = 0;
                for (int k = 0; k < NUM_AVERAGING_RUNS; k++) {
                    TIMING_NOW(start);
                    main_benchmark_loop(
                        comparison_pool, testPatterns[j].pattern, runConfig.num_items, num_freed[1], max_active[1]);
                    TIMING_NOW(stop);
                    TIMING_DIFF(elapsed, start, stop);
                    TIMING_ACCUM(accumulated, elapsed);
                }

                avg_time[1] = accumulated / NUM_AVERAGING_RUNS;
                ctor_count[1] = LargeObject::m_ctor_count;
                dtor_count[1] = LargeObject::m_dtor_count;

                getrusage(RUSAGE_SELF, &usage[1]);
            }

            // output results as JSON:
            if (json_ctx) {
                json_attr_object_begin(json_ctx, ("run_" + std::to_string(i + 1)).c_str());

                // test setup
                json_attr_double(json_ctx, "initial_size", runConfig.initial_size);
                json_attr_double(json_ctx, "enlarge_step", runConfig.enlarge_step);
                json_attr_double(json_ctx, "num_items", runConfig.num_items);

                // test results
                json_attr_object_begin(json_ctx, "boost_intrusive_pool");
                json_attr_double(json_ctx, "duration_nsec", avg_time[0]);
                json_attr_double(json_ctx, "duration_nsec_per_item", (double)avg_time[0] / (double)runConfig.num_items);
                json_attr_double(json_ctx, "num_items_freed", num_freed[0]);
                json_attr_double(json_ctx, "max_active_items", max_active[0]);
                json_attr_double(json_ctx, "max_rss", usage[0].ru_maxrss);
                json_attr_double(json_ctx, "ctor_count", ctor_count[0]);
                json_attr_double(json_ctx, "dtor_count", dtor_count[0]);
                json_attr_double(json_ctx, "num_resizings", num_resizings);
                json_attr_object_end(json_ctx); // boost_intrusive_pool_item

                // test results
                json_attr_object_begin(json_ctx, "plain_malloc");
                json_attr_double(json_ctx, "duration_nsec", avg_time[1]);
                json_attr_double(json_ctx, "duration_nsec_per_item", (double)avg_time[1] / (double)runConfig.num_items);
                json_attr_double(json_ctx, "num_items_freed", num_freed[1]);
                json_attr_double(json_ctx, "max_active_items", max_active[1]);
                json_attr_double(json_ctx, "max_rss", usage[1].ru_maxrss);
                json_attr_double(json_ctx, "ctor_count", ctor_count[1]);
                json_attr_double(json_ctx, "dtor_count", dtor_count[1]);
                json_attr_object_end(json_ctx); // plain_malloc
                json_attr_object_end(json_ctx); // run
            }
        }

        if (json_ctx)
            json_attr_object_end(json_ctx); // run
    }
}

static void do_json_benchmark()
{
    json_ctx_t json_ctx;

    json_init(&json_ctx, 0, stdout);
    json_document_begin(&json_ctx);
    do_benchmark(&json_ctx);
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

    // to better simulate a realistic workload use our own benchmarking routines to
    // defrag a little bit the memory of this process (but do not really write any output!)
    for (unsigned int i = 0; i < 3; i++)
        do_benchmark(NULL);

    // final run is to write real output JSON
    do_json_benchmark();

    return 0;
}
