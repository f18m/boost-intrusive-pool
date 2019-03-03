[![Build Status](https://travis-ci.com/f18m/boost-intrusive-pool.svg?branch=master)](https://travis-ci.com/f18m/boost-intrusive-pool)

# Boost Intrusive Pool
This project provides a C++ memory pool that is Boost-friendly and performance oriented.

## Features and Limitations
The `boost_intrusive_pool` provides the following features:
 - **smart pointers**: once "allocated" from the pool items whose reference count goes to zero return
   automatically to the pool;
 - **zero-malloc**: after a resize of `N` items, no memory allocations are EVER done until `M<=N` active
   items are in use;
 - O(1) allocate;
 - O(1) destroy (pool return);
 - use of standard, well-defined smart pointers: `boost::intrusive_ptr<>`; see [Boost documentation](https://www.boost.org/doc/libs/1_69_0/libs/smart_ptr/doc/html/smart_ptr.html#intrusive_ptr)
 - polymorphic-friendly pool: if A derives from `boost_intrusive_pool_item`, and B derives from A, the
   memory pool of B works just fine;
 - Header-only;
 - Provides two variants: the **infinite memory pool**, which automatically enlarges if the number of active items goes over 
   initial memory pool size, and the **bounded memory pool**, which just returns `NULL` if trying to allocate more active
   items than the configured limit;
 - **Optional** construction via an initialization function: when items are allocated out of the pool via the 
   `boost_intrusive_pool::allocate_through_init()` API, the `init()` member function of the memory-pooled objects 
   is called; C++11 perfect forwarding allows to pass optional parameters to the `init()` routine;
 - **Optional** construction via custom function: when items are allocated out of the pool via the 
   `boost_intrusive_pool::allocate_through_function()` the provided custom function is called with the memory-pooled 
   object as argument;
 - **Optional** recycling via custom function: when the pool is constructed, a custom function `std::function` can be
   specified; when items return to the pool it will be called with the item being recycled as parameter; this allows
   to perform special cleanup like releasing handles, clearing data structures, etc;
 - **Optional** recycling via alternative function: when items return to the pool, the memory pool can be configured
   to invoke the `destroy()` member function of the memory-pooled objects; this allows
   to perform special cleanup like releasing handles, clearing data structures, etc;

Of course there are tradeoffs in the design that bring in some limitations:
 - requires all C++ classes stored inside the memory pool to derive from a base class `boost_intrusive_pool_item`;
 - provides `boost::intrusive_ptr<>` instead of the more widely-used `std::shared_ptr<>`:
   reason is that `std::shared_ptr<>` puts the reference count in a separate block that needs a separate allocation
   and thus memory pools based on `std::shared_ptr<>` (like https://github.com/steinwurf/recycle) cannot be
   zero-malloc due to the heap-allocated control block;
 - requires C++ classes stored inside the memory pool to have a default constructor: reason is that to ensure
   the spatial locality of allocated items (for better cache / memory performances) we use the `new[]` operator 
   which does not allow to provide any parameter;
 - adds about 32 bytes of overhead to each C++ class to be stored inside the memory pool.


# How to Install

Since this project is header-only it does not need any specific installation, just grab the latest release and put the
`boost_intrusive_pool.hpp` file in your include path.

# Requirements

This templated memory pool requires a C++14 compliant compiler (it has been tested with GCC 7.x and 8.x).
It also requires Boost version 1.55 or higher.


# A Short Tutorial

The source code in [tests/tutorial.cpp](tests/tutorial.cpp) provides a short tutorial about the following topics:
 - `std::shared_ptr<>`
 - `boost::intrusive_ptr<>`
 - `memorypool::boost_intrusive_pool<>` (this project)
 
and shows that:

 - allocating an item through `std::shared_ptr<T>` results in the malloc of `sizeof(T)` plus about 16bytes
   (the control block);
 - allocating an item through `boost::intrusive_ptr<T>` results in the malloc of `sizeof(T)`: the refcount
   is not stored in any separate control block;
 - creation of a `memorypool::boost_intrusive_pool<>` results in several malloc operations, but then:
 - creation of an item `T` from a `memorypool::boost_intrusive_pool<T>` does not result in any malloc


# Example: Using the Default Constructor

In this example we show how to use memory-pooled objects that are initialized through their default constructor:

```
#include "boost_intrusive_pool.hpp"

void main()
{
	memorypool::boost_intrusive_pool<DummyClass> pool(4); // allocate pool of size 4
	   // this results in a big memory allocation; from now on instead it's a zero-malloc world:
	
	{
	    // allocate without ANY call at all (max performances):
	    boost::intrusive_ptr<DummyClass> hdummy = pool.allocate();
	
	    // of course copying pointers around does not trigger any memory allocation:
	    boost::intrusive_ptr<DummyClass> hdummy2 = hdummy;
	
	    // if we observed the pool now it would report: capacity=4, unused_count=3, inuse_count=1
	    
	    
	    // now instead allocate using the DummyClass default ctor:
	    boost::intrusive_ptr<DummyClass> hdummy3 = pool.allocate_through_init();
	    
	} // this time no memory free() will happen!

	// if we observed the pool now it would report: capacity=4, unused_count=4, inuse_count=0
}

```


# Example: Using a non-Default Constructor

In this example we show how to use memory-pooled objects that are initialized through their NON default constructor:

```
#include "boost_intrusive_pool.hpp"

void main()
{
	memorypool::boost_intrusive_pool<DummyClass> pool(4); // allocate pool of size 4
	   // this results in a big memory allocation; from now on instead it's a zero-malloc world:
	
	{
	    // now instead allocate using the DummyClass NON default ctor:
	    boost::intrusive_ptr<DummyClass> hdummy3 = pool.allocate_through_init(arg1, arg2, arg3);
	    
	} // this time no memory free() will happen!
}
```

# Performance Results

The following tables show results of some very simple benchmarking obtained on a desktop machine:

```
Intel(R) Core(TM) i5-4570 CPU @ 3.20GHz, 4 cores
16GB DDR3
libc-2.27 (Ubuntu 18.04)
```

The memory pool implementation is compared against a "no pool" solution (the `plain_malloc` line), which simply allocates
items directly from the heap through `malloc`.
For both the `boost_intrusive_pool` and the `plain_malloc` a very lightweight processing is simulated on the allocated
items so that these performance results show the gian you obtain if:
 - you intend to create a memory pool of large items, expensive to allocate each time;
 - the processing for each time is lightweight.

The benchmarks are then repeated considering 3 different memory allocators:
 1. [GNU libc](https://www.gnu.org/software/libc/) default malloc/free
 2. [Google perftools](https://github.com/gperftools/gperftools) also known as tcmalloc
 3. [Jemalloc](http://jemalloc.net/)

Moreover 2 different memory allocation/deallocation patterns are considered:
 1. `Continuous allocations, bulk free at end`: all 100 thousands large objects are allocated sequentially, lightweight-processed
    and then released back to the pool/heap.
 2. `Mixed alloc/free pattern`: the items are returned to the pool/heap in a pseudo-random order, potentially generating memory fragmentation
    in the `plain_malloc` implementation.

Results for the `Continuous allocations, bulk free at end` benchmark follow:

<table cellpadding="5" width="100%">
<tbody>
<tr>
<td width="50%">

![](tests/results/pattern_1_gnulibc.png)

</td>
<td width="50%">

![](tests/results/pattern_1_tcmalloc.png)

</td>
</tr>
<tr>
<td width="50%">

![](tests/results/pattern_1_jemalloc.png)


</td>
<td width="50%">

Results show that with glibc allocator (regular malloc/free implementation), the use of a memory
pool results in up to 44% improvement (from an average of 134ns to about 76ns).
When Google's tcmalloc or jemalloc allocators are in use the improvement grows to 67% and 76% respectively.

This is important to show that even if your software is using an optimized allocator the memory pool
pattern will improve performances considerably.

</td>
</tr>

</tbody>
</table>

Results for the `Mixed alloc/free pattern` benchmark follow:

<table cellpadding="5" width="100%">
<tbody>
<tr>
<td width="50%">

![](tests/results/pattern_2_gnulibc.png)

</td>
<td width="50%">

![](tests/results/pattern_2_tcmalloc.png)

</td>
</tr>
<tr>
<td width="50%">

![](tests/results/pattern_2_jemalloc.png)


</td>
<td width="50%">

These results show that with a pattern where malloc and free operations are scattered and "randomized" 
a little bit, regular allocators cannot avoid memory fragmentation and less spatial locality compared
to a memory pool implementation.
 
In particular improvements go from 40% (glibc) to 53% (jemalloc) and up to 73% (tcmalloc).

</td>
</tr>

</tbody>
</table>

Of course take all these performance results with care.
Actual performance gain may vary a lot depending on your rate of malloc/free operations, the pattern in which they happen,
the size of the pooled items, etc etc.
You can find the source code used to generate these benchmark results in the file [tests/performance_tests.cpp](tests/performance_tests.cpp).



# About Thread Safety

The memory pool has no locking mechanism whatsoever and is not thread safe.
The reason is that this memory pool is performance oriented and locks are not that fast, specially if you have many
threads continuosly allocating and releasing items to the pool.
In such a scenario, the best from a performance point of view, is to simply create a memory pool for each thread.


# Other Memory Pools

This memory pool implementation has been inspired by a few other C++ implementations out there like:

- https://github.com/steinwurf/recycle
- https://thinkingeek.com/2017/11/19/simple-memory-pool/
- https://github.com/cdesjardins/QueuePtr
