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

#include "boost_intrusive_pool.hpp"

#include <cstdint>
#include <functional>
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

    void init() { m_int = 0; }
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

void recycle_dummy_one(boost::intrusive_ptr<dummy_one> p)
{ /* nothing to do actually */
}

// Non Default constructible dummy object
struct dummy_two : public boost_intrusive_pool_item {
    dummy_two(uint32_t useless = 0) { ++m_count; }

    ~dummy_two() { --m_count; }

    static int32_t m_count;
};

int32_t dummy_two::m_count = 0;

void recycle_dummy_two(boost::intrusive_ptr<dummy_two> p)
{ /* nothing to do actually */
}

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
    } testArray[] = {
        { 10, 1 }, // force newline
        { 1, 100 }, // force newline
        { 100000, 1 }, // force newline
    };

    for (int i = 0; i < sizeof(testArray) / sizeof(testArray[0]); i++) {
        boost_intrusive_pool<DummyInt> f(
            testArray[i].initial_size /* initial size */, testArray[i].enlarge_step /* enlarge step */);

        BOOST_REQUIRE(!f.is_bounded());

        size_t num_elements = 10000, num_freed = 0, max_active = 0;
        std::map<int, HDummyInt> helper_container;
        for (unsigned int i = 0; i < num_elements; i++) {
            HDummyInt myInt = f.allocate_through_ctor(i);
            assert(myInt);

            f.check();

            *myInt = i;
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

        BOOST_REQUIRE(num_freed > 0);

        // This should hold always:
        //         m_free_count+m_inuse_count == m_total_count
        BOOST_REQUIRE_EQUAL(f.unused_count() + f.inuse_count(), f.capacity());

        BOOST_REQUIRE_EQUAL(f.inuse_count(), num_elements - num_freed);
        BOOST_REQUIRE(f.capacity() >= max_active);
        BOOST_REQUIRE(!f.empty());

        if (testArray[i].enlarge_step > 1)
            BOOST_REQUIRE(f.unused_count() > 0);

        if (testArray[i].initial_size < num_elements - num_freed)
            BOOST_REQUIRE(f.enlarge_steps_done() > 0);

        // IMPORTANT: this will crash as all pointers inside the map are INVALIDATED before
        //            the map clear() is called:
        /*
                f.clear();
                helper_container.clear(); // all pointers it contains have been invalidated!
        */

        helper_container.clear(); // all pointers it contains will be now released
        f.clear();

        BOOST_REQUIRE_EQUAL(f.inuse_count(), 0);
        BOOST_REQUIRE_EQUAL(f.capacity(), testArray[i].initial_size);
        BOOST_REQUIRE(f.empty());
        BOOST_REQUIRE_EQUAL(f.unused_count(), testArray[i].initial_size);
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

    for (int i = 0; i < sizeof(testArray) / sizeof(testArray[0]); i++) {
        boost_intrusive_pool<DummyInt> f(testArray[i].initial_size /* initial size */, 0 /* enlarge step */);
        std::vector<HDummyInt> helper_container;

        BOOST_REQUIRE(f.is_bounded());

        for (unsigned int j = 0; j < testArray[i].initial_size; j++) {
            HDummyInt myInt = f.allocate_through_ctor(3);
            assert(myInt);
            helper_container.push_back(myInt);

            f.check();
        }

        BOOST_REQUIRE_EQUAL(f.unused_count(), 0);

        // now if we allocate more we should fail gracefully:
        HDummyInt myInt = f.allocate_through_ctor(4);
        BOOST_REQUIRE(!myInt);

        // This should hold always:
        //         m_free_count+m_inuse_count == m_total_count
        BOOST_REQUIRE_EQUAL(f.unused_count() + f.inuse_count(), f.capacity());
        BOOST_REQUIRE_EQUAL(f.inuse_count(), testArray[i].initial_size);
        BOOST_REQUIRE_EQUAL(f.capacity(), testArray[i].initial_size);
        BOOST_REQUIRE(!f.empty());
        BOOST_REQUIRE_EQUAL(f.enlarge_steps_done(), 1); // just the initial one

        helper_container.clear();
        f.clear();

        BOOST_REQUIRE_EQUAL(f.inuse_count(), 0);
        BOOST_REQUIRE_EQUAL(f.capacity(), testArray[i].initial_size);
        BOOST_REQUIRE(f.empty());
    }
}

/// Test the basic API construct and free some objects
void test_api()
{
    {
        boost_intrusive_pool<dummy_one> pool;

        pool.set_recycle_method(RECYCLE_METHOD_DTOR);

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
        // pool.clear();
        // BOOST_REQUIRE(pool.unused_count(), 0U);
    }

    // BOOST_REQUIRE(dummy_one::m_count, 0);
}

/// Test that the pool works for non default constructable objects, if
/// we provide the allocator
void test_non_default_constructable()
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

        auto o1 = pool.allocate_through_ctor(3U);
        auto o2 = pool.allocate_through_ctor(3U);

        // the pool constructs BOOST_INTRUSIVE_POOL_DEFAULT_POOL_SIZE items
        // BUT then we call the default ctor 2 times more:
        BOOST_REQUIRE_EQUAL(dummy_two::m_count, BOOST_INTRUSIVE_POOL_DEFAULT_POOL_SIZE + 2);
    }

    // now all BOOST_INTRUSIVE_POOL_DEFAULT_POOL_SIZE items have been destroyed through
    // their dtor so it just remains an offset = 2 in the static instance count:
    BOOST_REQUIRE_EQUAL(dummy_two::m_count, 2);
}

boost::unit_test::test_suite* init_unit_test_suite(int argc, char* argv[])
{
    boost::unit_test::test_suite* test = BOOST_TEST_SUITE("Master test suite");

    test->add(BOOST_TEST_CASE(&infinite_memory_pool));
    test->add(BOOST_TEST_CASE(&bounded_memory_pool));
    test->add(BOOST_TEST_CASE(&test_api));
    test->add(BOOST_TEST_CASE(&test_non_default_constructable));

    return test;
}
