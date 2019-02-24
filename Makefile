CC=g++
CXXFLAGS= -std=c++17 -Iinclude -g -O0

DEPS = \
	include/boost_intrusive_pool.hpp \
	tests/tracing_malloc.h
BINS = \
	tests/tutorial \
	tests/unit_tests
	
all: $(BINS)

%.o: %.cpp $(DEPS)
	$(CC) $(CXXFLAGS) -c -o $@ $< 

tests/%: tests/%.o
	$(CC) -o $@ $^ $(CXXFLAGS)

clean:
	rm -f $(BINS)
