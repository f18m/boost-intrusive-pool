/*
 * Set of unit tests for the memorypool::boost_intrusive_pool
 * Some unit tests have been adapted from:
 *    https://github.com/steinwurf/recycle
 *
 * Note that to be able to take advantage of TravisCI/github integration
 * we only use features of Boost.test up to version 1.58
 *
 * Author: fmontorsi
 * Created: Feb 2019
 * License: BSD license
 *
 */

//------------------------------------------------------------------------------
// Includes
//------------------------------------------------------------------------------

#define BOOST_INTRUSIVE_POOL_DEBUG_CHECKS 1
#include "boost_intrusive_pool.hpp"

#include <cstdint>
#include <functional>
#include <malloc.h>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>

#define BOOST_REQUIRE_MODULE "main4"
#include <boost/test/included/unit_test.hpp>

using namespace memorypool;

//------------------------------------------------------------------------------
// Dummy test objects
//------------------------------------------------------------------------------

class DummyInt : public boost_intrusive_pool_item {
public:
    DummyInt(int j = 0)
        : m_int(j)
    {
    }

    DummyInt(const DummyInt&) = default;

    void init(int j = 0) { m_int = j; }
    void destroy() { m_int = 0; }

    bool operator==(const DummyInt& x) const { return x.m_int == m_int; }

private:
    int m_int;
};

typedef boost::intrusive_ptr<DummyInt> HDummyInt;

// Default constructible dummy object
struct dummy_one : public boost_intrusive_pool_item {
    dummy_one() { ++m_count; }

    ~dummy_one() { --m_count; }

    void init() {}

    // Counter which will check how many object have been allocate
    // and deallocated
    static int32_t m_count;
};

int32_t dummy_one::m_count = 0;

void recycle_dummy_one(boost::intrusive_ptr<dummy_one> p) { /* nothing to do actually */}

// Non Default constructible dummy object
struct dummy_two : public boost_intrusive_pool_item {
    dummy_two(uint32_t useless = 0) { ++m_count; }

    ~dummy_two() { --m_count; }

    void init(uint32_t useless = 0) {}

    static int32_t m_count;
};

int32_t dummy_two::m_count = 0;

void recycle_dummy_two(boost::intrusive_ptr<dummy_two> p) { /* nothing to do actually */}

// enable_shared_from_this dummy object
struct dummy_three : std::enable_shared_from_this<dummy_three>, public boost_intrusive_pool_item {

    dummy_three() { ++m_count; }

    ~dummy_three() { --m_count; }

    // Counter which will check how many object have been allocate
    // and deallocated
    static int32_t m_count;
};

int32_t dummy_three::m_count = 0;

//------------------------------------------------------------------------------
// Actual testcases
//------------------------------------------------------------------------------

void infinite_memory_pool()
{
    BOOST_TEST_MESSAGE("Starting boost_intrusive_pool<> tests when memory pool is unbounded");

    struct {
        unsigned int initial_size;
        unsigned int enlarge_step;
        unsigned int num_elements;
    } testArray[] = {
        { 10, 1, (int)1e6 }, // force newline
        { 1, 100, (int)5e4 }, // force newline
        { 100000, 1, 123456 }, // force newline
    };

    for (int testIdx = 0; testIdx < sizeof(testArray) / sizeof(testArray[0]); testIdx++) {
        BOOST_TEST_MESSAGE(std::string("Starting test entry #") + std::to_string(testIdx));

        boost_intrusive_pool<DummyInt> f(
            testArray[testIdx].initial_size /* initial size */, testArray[testIdx].enlarge_step /* enlarge step */);

        BOOST_REQUIRE(!f.is_bounded());
        f.check();

        size_t num_freed = 0, max_active = 0;
        std::map<int, HDummyInt> helper_container;
        for (unsigned int j = 0; j < testArray[testIdx].num_elements; j++) {
            HDummyInt myInt = f.allocate_through_init(j);
            assert(myInt);

            f.check();

            *myInt = j;
            helper_container[j] = myInt;

            // returns to the factory a few items in pseudo-random order
            if ((j % 7) == 0 || (j % 53) == 0 || (j % 12345) == 0) {
                size_t value_to_release = j / 10;

                auto it = helper_container.find(value_to_release);
                if (it != helper_container.end()) {
                    // erasing the smart pointer from the std::map will trigger its return to the memory pool:
                    helper_container.erase(value_to_release);
                    num_freed++;
                }
            }

            max_active = std::max(max_active, helper_container.size());
        }

        f.check();

        BOOST_REQUIRE(!f.is_memory_exhausted());
        BOOST_REQUIRE(num_freed > 0);

        // This should hold always:
        //         m_free_count+m_inuse_count == m_total_count
        BOOST_REQUIRE_EQUAL(f.unused_count() + f.inuse_count(), f.capacity());

        BOOST_REQUIRE_EQUAL(f.inuse_count(), testArray[testIdx].num_elements - num_freed);
        BOOST_REQUIRE(f.capacity() >= max_active);
        BOOST_REQUIRE(!f.empty());

        if (testArray[testIdx].enlarge_step > 1)
            BOOST_REQUIRE(f.unused_count() > 0);

        if (testArray[testIdx].initial_size < testArray[testIdx].num_elements - num_freed)
            BOOST_REQUIRE(f.enlarge_steps_done() > 0);

        // IMPORTANT: this will crash as all pointers inside the map are INVALIDATED before
        //            the map clear() is called:
        /*
                f.clear();
                helper_container.clear(); // all pointers it contains have been invalidated!
        */

        helper_container.clear(); // all pointers it contains will be now released

        // repeat twice for test:
        f.clear();
        f.clear();

        f.check();

        BOOST_REQUIRE_EQUAL(f.inuse_count(), 0);
        BOOST_REQUIRE_EQUAL(f.capacity(), 0);
        BOOST_REQUIRE(f.empty());
        BOOST_REQUIRE_EQUAL(f.unused_count(), 0);
    }
}

void bounded_memory_pool()
{
    BOOST_TEST_MESSAGE("Starting boost_intrusive_pool<> tests when memory pool is bounded");

    struct {
        unsigned int initial_size;
    } testArray[] = {
        { 1 }, // force newline
        { 10 }, // force newline
        { 100000 }, // force newline
    };

    for (int testIdx = 0; testIdx < sizeof(testArray) / sizeof(testArray[0]); testIdx++) {
        BOOST_TEST_MESSAGE(std::string("Starting test entry #") + std::to_string(testIdx));

        boost_intrusive_pool<DummyInt> f(testArray[testIdx].initial_size /* initial size */, 0 /* enlarge step */);
        std::vector<HDummyInt> helper_container;

        BOOST_REQUIRE(f.is_bounded());

        for (unsigned int j = 0; j < testArray[testIdx].initial_size; j++) {
            HDummyInt myInt = f.allocate_through_init(3);
            assert(myInt);
            helper_container.push_back(myInt);

            f.check();
        }

        BOOST_REQUIRE_EQUAL(f.unused_count(), 0);

        // now if we allocate more we should fail gracefully:
        HDummyInt myInt = f.allocate_through_init(4);
        BOOST_REQUIRE(!myInt);

        // This should hold always:
        //         m_free_count+m_inuse_count == m_total_count
        BOOST_REQUIRE_EQUAL(f.unused_count() + f.inuse_count(), f.capacity());
        BOOST_REQUIRE_EQUAL(f.inuse_count(), testArray[testIdx].initial_size);
        BOOST_REQUIRE_EQUAL(f.capacity(), testArray[testIdx].initial_size);
        BOOST_REQUIRE(!f.empty());
        BOOST_REQUIRE_EQUAL(f.enlarge_steps_done(), 1); // just the initial one

        helper_container.clear();
        f.clear();

        f.check();

        BOOST_REQUIRE_EQUAL(f.inuse_count(), 0);
        BOOST_REQUIRE_EQUAL(f.capacity(), 0);
        BOOST_REQUIRE(f.empty());
    }
}

/// Test the basic API construct and free some objects
void test_api()
{
    {
        uint32_t recycled = 0;
        boost_intrusive_pool<dummy_one> pool;

        auto recycle_fn = [&recycled](dummy_one& object) {
            BOOST_REQUIRE(object.m_count > 0);
            object.m_count--;
            ++recycled;
        };

        pool.set_recycle_method(RECYCLE_METHOD_CUSTOM_FUNCTION, recycle_fn);

        BOOST_REQUIRE_EQUAL(pool.unused_count(), BOOST_INTRUSIVE_POOL_DEFAULT_POOL_SIZE);

        {
            auto d1 = pool.allocate();
            BOOST_REQUIRE_EQUAL(pool.unused_count(), BOOST_INTRUSIVE_POOL_DEFAULT_POOL_SIZE - 1);
        }

        BOOST_REQUIRE_EQUAL(pool.unused_count(), BOOST_INTRUSIVE_POOL_DEFAULT_POOL_SIZE);

        auto d2 = pool.allocate();

        BOOST_REQUIRE_EQUAL(pool.unused_count(), BOOST_INTRUSIVE_POOL_DEFAULT_POOL_SIZE - 1);

        auto d3 = pool.allocate();

        BOOST_REQUIRE_EQUAL(pool.unused_count(), BOOST_INTRUSIVE_POOL_DEFAULT_POOL_SIZE - 2);

        // the dummy_one::m_count counter is off by 1 because above we called an allocate() and
        // then destroyed the "d1" pointer: that resulted in the item being recycled and dummy_one::~dummy_one
        // being called
        BOOST_REQUIRE_EQUAL(dummy_one::m_count, BOOST_INTRUSIVE_POOL_DEFAULT_POOL_SIZE - 1);

        {
            auto d4 = pool.allocate();
            BOOST_REQUIRE_EQUAL(pool.unused_count(), BOOST_INTRUSIVE_POOL_DEFAULT_POOL_SIZE - 3);
        }

        BOOST_REQUIRE_EQUAL(pool.unused_count(), BOOST_INTRUSIVE_POOL_DEFAULT_POOL_SIZE - 2);

        // FIXME FIXME THIS DOES NOT WORK CURRENTLY SINCE ALL SMART POINTER DTORS
        // ARE STILL ALIVE AT THIS POINT
        pool.clear();
        BOOST_REQUIRE_EQUAL(pool.unused_count(), 0U);
    }

    // BOOST_REQUIRE(dummy_one::m_count, 0);
}

void test_allocate_methods()
{
    {
        boost_intrusive_pool<dummy_two> pool;

        auto o1 = pool.allocate();
        auto o2 = pool.allocate();

        // the pool constructs BOOST_INTRUSIVE_POOL_DEFAULT_POOL_SIZE items
        // and then we did not call any ctor (using allocate() nothing gets called inside
        // the dummy_two class):
        BOOST_REQUIRE_EQUAL(dummy_two::m_count, BOOST_INTRUSIVE_POOL_DEFAULT_POOL_SIZE);
    }

    // now all BOOST_INTRUSIVE_POOL_DEFAULT_POOL_SIZE items have been destroyed through
    // their dtor so the count returns to zero:
    BOOST_REQUIRE_EQUAL(dummy_two::m_count, 0);

    {
        boost_intrusive_pool<dummy_two> pool;

        auto o1 = pool.allocate_through_init(3U);
        auto o2 = pool.allocate_through_init(3U);

        // the pool constructs BOOST_INTRUSIVE_POOL_DEFAULT_POOL_SIZE items
        BOOST_REQUIRE_EQUAL(dummy_two::m_count, BOOST_INTRUSIVE_POOL_DEFAULT_POOL_SIZE);
    }

    // now all BOOST_INTRUSIVE_POOL_DEFAULT_POOL_SIZE items have been destroyed through
    // their dtor so it just remains an offset = 2 in the static instance count:
    BOOST_REQUIRE_EQUAL(dummy_two::m_count, 0);
}

/// Test that everything works even if the pool dies before the
/// objects allocated
void pool_die_before_object()
{
    {
        boost::intrusive_ptr<dummy_one> d1;
        boost::intrusive_ptr<dummy_one> d2;
        boost::intrusive_ptr<dummy_one> d3;

        dummy_one::m_count = 0;

        {
            boost_intrusive_pool<dummy_one> pool;

            d1 = pool.allocate();
            d2 = pool.allocate();
            d3 = pool.allocate();

            BOOST_REQUIRE_EQUAL(dummy_one::m_count, BOOST_INTRUSIVE_POOL_DEFAULT_POOL_SIZE);
        }

        BOOST_REQUIRE_EQUAL(dummy_one::m_count, BOOST_INTRUSIVE_POOL_DEFAULT_POOL_SIZE);
    }

    BOOST_REQUIRE_EQUAL(dummy_one::m_count, 0);
}

void overwrite_pool_items_with_other_pool_items()
{
    boost_intrusive_pool<DummyInt> pool(64, 16, memorypool::RECYCLE_METHOD_DESTROY_FUNCTION);

    pool.check();

    {
        std::map<int, HDummyInt> helper_container;
        for (unsigned int j = 0; j < 1000; j++) {
            HDummyInt myInt = pool.allocate_through_init(j);
            assert(myInt);

            pool.check();

            // put the item inside a MAP container
            helper_container[j] = myInt;
        }

        pool.check();

        BOOST_REQUIRE(!pool.is_memory_exhausted());
        BOOST_REQUIRE_EQUAL(pool.inuse_count(), 1000);

        // now allocate an other block of items and check refcounting happens correctly so that
        // if we overwrite a filled map entry, the previous pointer is erased correctly
        for (unsigned int j = 0; j < 500; j++) {
            HDummyInt myInt = pool.allocate_through_init(j);
            assert(myInt);

            pool.check();

            // put the item inside a MAP container
            helper_container[j] = myInt;
        }

        pool.check();

        BOOST_REQUIRE(!pool.is_memory_exhausted());
        BOOST_REQUIRE_EQUAL(
            pool.inuse_count(), 1000); // should be still 1000: we allocated 500 more but released other 500 entries!

        helper_container.clear(); // all pointers it contains will be now released
    }

    BOOST_REQUIRE_EQUAL(pool.inuse_count(), 0);

    // repeat twice for test:
    pool.clear();
    pool.clear();

    pool.check();

    BOOST_REQUIRE_EQUAL(pool.inuse_count(), 0);
    BOOST_REQUIRE_EQUAL(pool.capacity(), 0);
    BOOST_REQUIRE(pool.empty());
    BOOST_REQUIRE_EQUAL(pool.unused_count(), 0);
}

boost::unit_test::test_suite* init_unit_test_suite(int argc, char* argv[])
{
    // about M_PERTURB:
    // If this parameter is set to a nonzero value, then bytes of allocated memory
    // are scrambled; This can be useful for detecting
    // errors where programs incorrectly rely on allocated memory
    // being initialized to zero, or reuse values in memory that has
    // already been freed.
    mallopt(M_PERTURB, 1);

    boost::unit_test::test_suite* test = BOOST_TEST_SUITE("Master test suite");

    test->add(BOOST_TEST_CASE(&infinite_memory_pool));
    test->add(BOOST_TEST_CASE(&bounded_memory_pool));
    test->add(BOOST_TEST_CASE(&test_api));
    test->add(BOOST_TEST_CASE(&test_allocate_methods));
    test->add(BOOST_TEST_CASE(&pool_die_before_object));
    test->add(BOOST_TEST_CASE(&overwrite_pool_items_with_other_pool_items));

    return test;
}
