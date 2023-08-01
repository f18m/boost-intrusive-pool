/*
 * Small example program to show the advantage of the
 * boost_intrusive_pool compared to e.g. direct use of
 * std::shared_ptr<>
 *
 * Author: fmontorsi
 * Created: Feb 2019
 * License: BSD license
 *
 */

//------------------------------------------------------------------------------
// Includes
//------------------------------------------------------------------------------

#include "boost_intrusive_pool.hpp"
#include "tracing_malloc.h"

#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <pthread.h>
#include <string>
#include <unordered_map>
#include <vector>

//------------------------------------------------------------------------------
// Utility classes
//------------------------------------------------------------------------------

class DummyClass : public memorypool::boost_intrusive_pool_item {
public:
    DummyClass(uint32_t n = 0)
    {
        buf[0] = 'a' + (n & 0x11);
        buf[1023] = (n >> 24);
        TRACE_METHOD();
    }
    ~DummyClass()
    {
        TRACE_METHOD(); // this shows that dtor runs every time an instance returns to the pool!
    }

    virtual char dummy() const { return buf[0]; }
    void init(uint32_t n = 0) { buf[0] = 'a' + (n & 0x11); }

private:
    // just some fat buffer:
    char buf[1024];
};

class DummyDerivedClass : public DummyClass {
public:
    DummyDerivedClass(uint32_t n = 0)
        : DummyClass(n)
    {
        bag[0] = 'A' + n;
        TRACE_METHOD();
    }
    ~DummyDerivedClass()
    {
        TRACE_METHOD(); // this shows that dtor runs every time an instance returns to the pool!
    }

    virtual char dummy() const { return (char)bag[0]; }
    void init(uint32_t n = 0) { bag[0] = 'A' + n; }
    void yet_another_init_fun(uint32_t n = 0) { bag[0] = 'A' + n; }

private:
    // yet another fat buffer:
    size_t bag[2048];
};

//------------------------------------------------------------------------------
// Utility functions
//------------------------------------------------------------------------------

template <class Item> void observe_pool(const memorypool::boost_intrusive_pool<Item>& pool)
{
    std::cout << "  The pool now has capacity=" << pool.capacity() << ", unused_count=" << (size_t)pool.unused_count()
              << ", inuse_count=" << pool.inuse_count() << std::endl;
}

//------------------------------------------------------------------------------
// Showcase routines
//------------------------------------------------------------------------------

void showcase_std_shared_pointers()
{
    print_header();
    std::cout << "Running some examples for std::shared_ptr<>:" << std::endl;

    // the test below shows that the overhead of a std::shared_ptr is about 8B/pointer
    // (tested with GCC 7.3.0) and the control block adds an overhead of about 16B

    {
        std::shared_ptr<DummyClass> hdummy;
        std::cout << "  Size of a simple std::shared_ptr<>: " << sizeof(hdummy) << std::endl;

        {
            // now allocate an instance of the class:
            std::cout << "  Now allocating dummy class of size: " << sizeof(DummyClass) << std::endl;
            hdummy = std::make_shared<DummyClass>(); // you will see a memory malloc traced when running this line

            // of course copying pointers around does not trigger any memory allocation:
            std::shared_ptr<DummyClass> hdummy2 = hdummy;

        } // the DummyClass instance survives this block since its reference is stored in hdummy

        std::cout << "  Going to release all references to the std::shared_ptr<> created so far" << std::endl;
    } // you will see a memory free traced when running this line
}

void showcase_boost_intrusive_pointers()
{
    print_header();
    std::cout << "Running some examples for boost::intrusive_ptr<>:" << std::endl;

    {
        std::cout << "  Now allocating dummy class of size: " << sizeof(DummyClass) << std::endl;

        boost::intrusive_ptr<DummyClass> hdummy(new DummyClass(3));
        // you will see a memory malloc traced when running this line

        std::cout << "  Size of a simple boost::intrusive_ptr<>: " << sizeof(hdummy) << std::endl;

        {
            // of course copying pointers around does not trigger any memory allocation:
            boost::intrusive_ptr<DummyClass> hdummy2 = hdummy;
            std::cout << "  Value from allocated dummy class: " << hdummy2->dummy() << std::endl;

            // of course you can move Boost intrusive pointers as well:
            std::cout << "  Before std::move hdummy2 is valid: " << (bool)hdummy2 << std::endl;
            boost::intrusive_ptr<DummyClass> hdummy3(std::move(hdummy2));
            std::cout << "  Value from allocated dummy class: " << hdummy3->dummy() << std::endl;
            std::cout << "  After std::move hdummy2 is valid: " << (bool)hdummy2 << std::endl;

        } // the DummyClass instance survives this block since its reference is stored in hdummy

        std::cout << "  Going to release all references to the boost::intrusive_ptr<> created so far" << std::endl;
    } // you will see a memory free traced when running this line
}

void showcase_boost_intrusive_pool()
{
    print_header();

    {
        std::cout << "  Now allocating a new memorypool::boost_intrusive_pool<>. A large malloc will happen."
                  << std::endl;

        memorypool::boost_intrusive_pool<DummyClass> pool(4, 1);
        // you will see a memory malloc traced when running this line

        std::cout << "  Boost Intrusive Pool for DummyClass has size: " << sizeof(pool) << std::endl;

        {
            std::cout << "  Now allocating dummy class of size: " << sizeof(DummyClass)
                      << " from the memory pool. This time no mallocs will happen!" << std::endl;
            boost::intrusive_ptr<DummyClass> hdummy = pool.allocate();

            // of course copying pointers around does not trigger any memory allocation:
            boost::intrusive_ptr<DummyClass> hdummy2 = hdummy;
            std::cout << "  Value from allocated dummy class constructed via default ctor: " << hdummy2->dummy()
                      << std::endl;

            observe_pool(pool);

            std::cout << "  Going to release the references to the boost::intrusive_ptr<> created so far. This time no "
                         "free() will happen!"
                      << std::endl;

        } // this time no memory free() will happen!

        observe_pool(pool);

        std::cout << "  Going to release the whole memory pool. You will see a bunch of dtor and memory free happen!"
                  << std::endl;
    } // you will see a memory free traced when running this line

    print_header();

    // now showcase a memory pool of objects having the following class hierarchy:
    //   boost_intrusive_pool_item
    //   \--- DummyClass
    //        \--- DummyDerivedClass

    {
        std::cout << "  Now allocating a new memorypool::boost_intrusive_pool<>. A large malloc will happen."
                  << std::endl;
        memorypool::boost_intrusive_pool<DummyDerivedClass> pool(4, 1);

        {
            std::cout << "  Now allocating derived dummy class of size: " << sizeof(DummyDerivedClass)
                      << " from the memory pool. This time no mallocs will happen but the ctor will get called again!"
                      << std::endl;

            size_t initializer_value = 3;

            auto custom_init_fn
                = [&initializer_value](DummyDerivedClass& object) { object.yet_another_init_fun(initializer_value); };

            // just for fun this time we allocate the object using non-default constructor:
            boost::intrusive_ptr<DummyDerivedClass> hdummy = pool.allocate_through_function(custom_init_fn);

            std::cout << "  Value from allocated dummy class constructed via NON default ctor: " << hdummy->dummy()
                      << std::endl;

            observe_pool(pool);

            std::cout << "  Going to release the references to the boost::intrusive_ptr<> created so far. This time no "
                         "free() will happen, just a dtor call!"
                      << std::endl;

        } // this time no memory free() will happen!

        observe_pool(pool);

        std::cout << "  Going to release the whole memory pool. You will see a bunch of dtor and memory free happen!"
                  << std::endl;
    } // you will see a memory free traced when running this line

    std::cout << "Note that the overhead of memory pool support is sizeof(memorypool::boost_intrusive_pool_item)="
              << sizeof(memorypool::boost_intrusive_pool_item) << "bytes" << std::endl;
}

//------------------------------------------------------------------------------
// Main
//------------------------------------------------------------------------------

int main()
{
    showcase_std_shared_pointers();
    showcase_boost_intrusive_pointers();
    showcase_boost_intrusive_pool();

    print_header();
    std::cout << "Exiting" << std::endl;
    return 0;
}
