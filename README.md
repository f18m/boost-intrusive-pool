[![Build Status](https://travis-ci.com/f18m/boost-intrusive-pool.svg?branch=master)](https://travis-ci.com/f18m/boost-intrusive-pool)

# Boost Intrusive Pool
This project provides a C++ memory pool that is Boost-friendly and performance oriented.

## Features and Limitations
The boost_intrusive_pool provides the following features:
 - smart pointer pool: once "allocated" from the pool items whose ref count goes to zero return
   automatically to the pool.
 - zero-malloc: after a resize of N items, no memory allocations are EVER done until M<=N active
   items are in use
 - O(1) allocate
 - O(1) destroy (pool return)
 - use of standard, well-defined smart pointers: boost::intrusive_ptr<>
 - polymorphic-friendly pool: if A derives from boost_intrusive_pool_item, and B derives from A, the
   memory pool of B just works
 - OPTIONAL standard construction: when items are taken out the pool, their ctor is called
   when the boost_intrusive_pool::allocate_through_ctor() is called; C++11 perfect forwarding allows to
   pass optional parameters to the ctor routine
 - OPTIONAL construction via alternative function: when items are taken out the pool, their init() is called
   when the boost_intrusive_pool::allocate_through_init() is called; C++11 perfect forwarding allows to
   pass optional parameters to the init() routine
 - OPTIONAL standard recycling: when items return to the pool, their dtor is called if no other destructor
   function is provided
 - OPTIONAL recycling via custom function: when the pool is constructed, a destructor std::function can be
   specified; when items return to the pool it will be called with the item being recycled as parameter
 - Header-only

Of course there are tradeoffs in the design that bring in some limitations:
 - requires all C++ classes stored inside the memory pool to derive from a base class "boost_intrusive_pool_item"
 - provides boost::intrusive_ptr<> instead of the more widely-used std::shared_ptr<>:
   reason is that std::shared_ptr<> puts the reference count in a separate block that needs a separate allocation
   and thus memory pools based on std::shared_ptr<> (like https://github.com/steinwurf/recycle) cannot be
   zero-malloc
 - requires C++ classes stored inside the memory pool to have a default constructor: reason is that to ensure
   the spatial locality of allocated items (for better cache / memory performances) we use the new[] operator 
   which does not allow to provide any parameter
 - adds about 24 bytes of overhead to each C++ class to be stored inside the memory pool
 
The templated memory pool has been tested with C++14 and C++17 with recent GCC versions (7.x and 8.x).


# How to Install

Since this project is header-only you don't need any specific installation, just grab the latest release and put the
"boost_intrusive_pool.hpp" file in your include path.


# Example: Using the Default Constructor

In this example we show how to use memory-pooled objects that are initialized through their default constructor:

```

```


# Example: Using a non-Default Constructor

In this example we show how to use memory-pooled objects that are initialized through their default constructor:

```

```



# About Thread Safety

The memory pool has no locking mechanism whatsoever and is not thread safe.
The reason is that this memory pool is performance oriented and locks are not that fast, specially if you have many
threads continuosly allocating and releasing items to the pool.
In such a scenario, the best from a performance point of view, is to simply create a memory pool for each thread.
