# Very simple quick-and-dirty makefile to build tutorial program and unit tests
# Assumes Boost and GCC are available in standard paths.

CC=g++
CXXFLAGS= -fPIC -std=c++17 -Iinclude -g -O0

DEPS = \
	include/boost_intrusive_pool.hpp \
	tests/tracing_malloc.h
BINS = \
	tests/tutorial \
	tests/unit_tests \
	tests/performance_tests

# Targets

all: $(BINS)
	@echo "Run tests/tutorial for a short tutorial (read comments in the source code!)"

test: $(BINS)
	tests/unit_tests --log_level=all --show_progress
	
tests: test

clean:
	rm -f $(BINS) tests/*.o

.PHONY: all test tests clean


# Rules

%.o: %.cpp $(DEPS)
	$(CC) $(CXXFLAGS) -c -o $@ $< 

tests/%: tests/%.o
	$(CC) -o $@ $^ $(CXXFLAGS)

tests/performance_tests: tests/performance_tests.o tests/json-lib.o
	$(CC) -o $@ tests/performance_tests.o tests/json-lib.o $(CXXFLAGS)
