# Very simple quick-and-dirty makefile to build tutorial program and unit tests
# Assumes Boost and GCC are available in standard paths.

CC=g++
CXXFLAGS_OPT= -fPIC -std=c++14 -Iinclude -O3
CXXFLAGS_DBG= -fPIC -std=c++14 -Iinclude -g -O0
DEBUGFLAGS= -DBOOST_INTRUSIVE_POOL_DEBUG_CHECKS=1 -DBOOST_INTRUSIVE_POOL_DEBUG_THREAD_ACCESS=1

DEPS = \
	include/boost_intrusive_pool.hpp \
	tests/tracing_malloc.h
BINS = \
	tests/tutorial \
	tests/unit_tests \
	tests/performance_tests
	
	
# Constants for performance tests:

# tested on Ubuntu 18.04
#    apt install libtcmalloc-minimal4 libjemalloc1
LIBTCMALLOC_LOCATION := /usr/lib/x86_64-linux-gnu/libtcmalloc_minimal.so.4
LIBJEMALLOC_LOCATION := /usr/lib/x86_64-linux-gnu/libjemalloc.so.1

VALGRIND_LOGFILE_POSTFIX:=unit-tests-$(shell date +%F-%H%M%S)
VALGRIND_SUPP:=valgrind.supp
VALGRIND_COMMON_OPTS:=--gen-suppressions=all --time-stamp=yes --error-limit=no
 # --suppressions=$(VALGRIND_SUPP)
MEMCHECK_COMMON_OPTS:=--tool=memcheck $(VALGRIND_COMMON_OPTS) --track-origins=yes --malloc-fill=AF --free-fill=BF --leak-check=full

# Targets

all: $(BINS)
	@echo "Run tests/tutorial for a short tutorial (read comments in the source code!)"

test: $(BINS)
	tests/unit_tests --log_level=all --show_progress

# just a synonim for "test":
tests: test

test_valgrind:
	valgrind $(MEMCHECK_COMMON_OPTS) --log-file=valgrind-$(VALGRIND_LOGFILE_POSTFIX).log tests/unit_tests


benchmarks: 
	@echo "Running the performance benchmarking tool without any optimized allocator:"
	tests/performance_tests     >tests/results/bench_results_gnulibc.json
	@echo "Now running the performance benchmarking tool using some optimized allocator (must be installed systemwide!):"
	LD_PRELOAD="$(LIBTCMALLOC_LOCATION)" tests/performance_tests    >tests/results/bench_results_tcmalloc.json
	LD_PRELOAD="$(LIBJEMALLOC_LOCATION)" tests/performance_tests    >tests/results/bench_results_jemalloc.json
	
plots:
	tests/bench_plot_results.py gnulibc tests/results/bench_results_gnulibc.json
	tests/bench_plot_results.py tcmalloc tests/results/bench_results_tcmalloc.json
	tests/bench_plot_results.py jemalloc tests/results/bench_results_jemalloc.json

clean:
	rm -f $(BINS) tests/*.o

.PHONY: all test tests clean


# Rules

%.o: %.cpp $(DEPS)
	$(CC) $(CXXFLAGS_DBG) $(DEBUGFLAGS) -c -o $@ $< 

# when compiling unit tests CPP also include DEBUGFLAGS to increase amount of checks we do:
tests/unit_tests.o: tests/unit_tests.cpp
	$(CC) $(CXXFLAGS_DBG) $(DEBUGFLAGS) -c -o $@ $<

# when compiling unit tests CPP also include DEBUGFLAGS to increase amount of checks we do:
tests/performance_tests.o: tests/performance_tests.cpp
	$(CC) $(CXXFLAGS_OPT) -c -o $@ $<
tests/json-lib.o: tests/json-lib.cpp
	$(CC) $(CXXFLAGS_OPT) -c -o $@ $<

tests/%: tests/%.o
	$(CC) -o $@ $^

tests/performance_tests: tests/performance_tests.o tests/json-lib.o
	$(CC) -o $@ tests/performance_tests.o tests/json-lib.o $(CXXFLAGS)

