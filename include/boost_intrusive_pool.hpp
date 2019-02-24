/*
 * A C++ memory pool that is Boost-friendly and performance oriented.
 *
 * Inspired by:
 *  - https://thinkingeek.com/2017/11/19/simple-memory-pool/: a C++ memory pool for raw pointers
 *    (no smart pointers) using free lists and perfect forwarding to handle non-default ctors.
 *  - https://github.com/steinwurf/recycle: a C++ memory pool for std::shared_ptr<>, which however
 *    produces 1 malloc operation for each allocation/recycle operation
 *
 * This memory pool implementation provides all following features:
 *  - smart pointer pool: once "allocated" from the pool items whose ref count goes to zero return
 *    automatically to the pool.
 *  - zero-malloc: after a resize of N items, no memory allocations are EVER done until M<=N active
 *    items are in use
 *  - O(1) allocate
 *  - O(1) destroy (pool return)
 *  - use of standard, well-defined smart pointers: boost::intrusive_ptr<>
 *  - polymorphic-friendly pool: if A derives from boost_intrusive_pool_item, and B derives from A, the
 *    memory pool of B just works
 *  - OPTIONAL standard construction: when items are taken out the pool, their ctor is called
 *    when the boost_intrusive_pool::allocate_through_ctor() is called; C++11 perfect forwarding allows to
 *    pass optional parameters to the ctor routine
 *  - OPTIONAL construction via alternative function: when items are taken out the pool, their init() is called
 *    when the boost_intrusive_pool::allocate_through_init() is called; C++11 perfect forwarding allows to
 *    pass optional parameters to the init() routine
 *  - OPTIONAL standard recycling: when items return to the pool, their dtor is called if no other destructor
 *    function is provided
 *  - OPTIONAL recycling via custom function: when the pool is constructed, a destructor std::function can be
 *    specified; when items return to the pool it will be called with the item being recycled as parameter
 *
 * Main limitations:
 *  - provides boost::intrusive_ptr<> instead of the more widely-used std::shared_ptr<>:
 *    reason is that std::shared_ptr<> puts the reference count in a separate block that needs a separate allocation
 *    and thus memory pools based on std::shared_ptr<> (like https://github.com/steinwurf/recycle) cannot be
 *    zero-malloc
 *  - requires "Item" to have a default constructor: reason is that to ensure the spatial locality of allocated
 *    items (for better cache / memory performances) we use the new[] operator which does not allow to provide
 *    any parameter
 *
 * Author: fmontorsi
 * Created: Feb 2019
 * License: BSD license
 *
 */

#pragma once

//------------------------------------------------------------------------------
// Includes
//------------------------------------------------------------------------------

#include <list>
#include <memory>

// #include <boost/intrusive/slist.hpp> // not really used finally
#include <boost/smart_ptr/intrusive_ptr.hpp>
#include <boost/smart_ptr/intrusive_ref_counter.hpp>

namespace memorypool {

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

#define BOOST_INTRUSIVE_POOL_DEFAULT_POOL_SIZE (64)
#define BOOST_INTRUSIVE_POOL_INCREASE_STEP (64)

//------------------------------------------------------------------------------
// Forward declarations
//------------------------------------------------------------------------------

class boost_intrusive_pool_item;

//------------------------------------------------------------------------------
// boost_intrusive_pool_iface
//------------------------------------------------------------------------------

class boost_intrusive_pool_iface {
public:
    virtual ~boost_intrusive_pool_iface() = default;

    virtual void recycle(boost_intrusive_pool_item* item) = 0;
};

//------------------------------------------------------------------------------
// boost_intrusive_pool_item
//------------------------------------------------------------------------------

// template <class Item>
class boost_intrusive_pool_item
/*: public boost::intrusive::slist_base_hook<boost::intrusive::link_mode<boost::intrusive::auto_unlink>>,
                  : public boost::intrusive_ref_counter<boost_intrusive_pool_item, boost::thread_unsafe_counter>*/
{
    friend void intrusive_ptr_add_ref(boost_intrusive_pool_item* x);
    friend void intrusive_ptr_release(boost_intrusive_pool_item* x);

public:
    boost_intrusive_pool_item()
    {
        m_next = nullptr;
        m_refcount = 0;
        m_pool = nullptr;
    }
    boost_intrusive_pool_item(const boost_intrusive_pool_item& other)
    {
        // IMPORTANT:
        //   do not copy the members of this class around:
        //   whether "this" instance is inside a memory pool or not, that has to stay that way,
        //   regardless of whether "other" is inside a memory pool or not.

        m_next = nullptr;
        m_refcount = 0;
        m_pool = nullptr;
    }
    boost_intrusive_pool_item(const boost_intrusive_pool_item&& other)
    {
        // IMPORTANT:
        //   do not copy the members of this class around:
        //   whether "this" instance is inside a memory pool or not, that has to stay that way,
        //   regardless of whether "other" is inside a memory pool or not.

        m_next = nullptr;
        m_refcount = 0;
        m_pool = nullptr;
    }
    virtual ~boost_intrusive_pool_item() {}

    boost_intrusive_pool_item* _refcounted_item_get_next() { return m_next; }
    void _refcounted_item_set_next(boost_intrusive_pool_item* p) { m_next = p; }
    // void _refcounted_item_set_pool(std::shared_ptr<boost_intrusive_pool_iface> p)
    void _refcounted_item_set_pool(boost_intrusive_pool_iface* p) { m_pool = p; }

    virtual void init() {}

    boost_intrusive_pool_item& operator=(const boost_intrusive_pool_item& other)
    {
        // IMPORTANT:
        //   do not copy the members of this class around:
        //   whether "this" instance is inside a memory pool or not, that has to stay that way,
        //   regardless of whether "other" is inside a memory pool or not.
        return *this;
    }
    boost_intrusive_pool_item& operator=(const boost_intrusive_pool_item&& other)
    {
        // IMPORTANT:
        //   do not copy the members of this class around:
        //   whether "this" instance is inside a memory pool or not, that has to stay that way,
        //   regardless of whether "other" is inside a memory pool or not.
        return *this;
    }

private:
    boost_intrusive_pool_item* m_next; // we use a free-list-based memory pool algorithm

    size_t m_refcount; // intrusive refcount
    // std::shared_ptr<boost_intrusive_pool_iface> m_pool; // used for auto-return to the memory pool, when refcount
    // reaches zero
    boost_intrusive_pool_iface* m_pool;
};

inline void intrusive_ptr_add_ref(boost_intrusive_pool_item* x) { ++x->m_refcount; }

inline void intrusive_ptr_release(boost_intrusive_pool_item* x)
{
    if (--x->m_refcount == 0) {
        if (x->m_pool)
            x->m_pool->recycle(x);
        else
            // assume the item has been allocated out of the pool:
            delete x;
    }
}

//------------------------------------------------------------------------------
// boost_intrusive_pool
//------------------------------------------------------------------------------

// Arena of items. This is just an array of items and a pointer
// to another arena. All arenas are singly linked between them.
template <typename Item> class boost_intrusive_pool_arena {
public:
    // Creates an arena with arena_size items.
    boost_intrusive_pool_arena(size_t arena_size, boost_intrusive_pool_iface* p)
    {
        m_storage = new Item[arena_size];
        if (m_storage) {
            for (size_t i = 1; i < arena_size; i++) {
                m_storage[i - 1]._refcounted_item_set_next(&m_storage[i]);
                m_storage[i - 1]._refcounted_item_set_pool(p);
            }
            m_storage[arena_size - 1]._refcounted_item_set_next(nullptr);
            m_storage[arena_size - 1]._refcounted_item_set_pool(p);
        }
        // else: malloc failed!

        m_next_arena = nullptr;
    }

    boost_intrusive_pool_arena(const boost_intrusive_pool_arena& other) = delete;
    boost_intrusive_pool_arena(const boost_intrusive_pool_arena&& other) = delete;

    ~boost_intrusive_pool_arena()
    {
        // m_storage is automatically cleaned up
        if (m_storage) {
            delete[] m_storage;
            m_storage = nullptr;
        }
    }

    // Returns a pointer to the array of items. This is used by the arena
    // itself. This is only used to update free_list during initialization
    // or when creating a new arena when the current one is full.
    Item* get_storage() const
    {
        return m_storage; //.get();
    }

    // Sets the next arena. Used when the current arena is full and
    // we have created this one to get more storage.
    void set_next_arena(boost_intrusive_pool_arena* p)
    {
        assert(!m_next_arena);
        m_next_arena = p;
    }
    boost_intrusive_pool_arena* get_next_arena() { return m_next_arena; }

    boost_intrusive_pool_arena operator=(const boost_intrusive_pool_arena& other) = delete;
    boost_intrusive_pool_arena operator=(const boost_intrusive_pool_arena&& other) = delete;

private:
    // Pointer to the next arena.
    boost_intrusive_pool_arena* m_next_arena;

    // Storage of this arena.
    // std::unique_ptr<Item[]> m_storage;
    Item* m_storage;
};

//------------------------------------------------------------------------------
// boost_intrusive_pool
//------------------------------------------------------------------------------

template <class Item> class boost_intrusive_pool : public boost_intrusive_pool_iface {
public:
    // using dummy = typename std::enable_if<std::is_base_of<boost_intrusive_pool_item, Item>::value>::type;

    /// The pointer to the resource
    using item_ptr = boost::intrusive_ptr<Item>;

    /// The base class of each item
    // using item_base = boost_intrusive_pool_item<Item>;

    /// The list of pools
    // using pool_impl_type = boost::intrusive::slist<Item, boost::intrusive::constant_time_size<false>>;
    // using pool_algo = boost::intrusive::linear_slist_algorithms<refcounted_object_base>;

    /// The allocate function type
    /// Should take no arguments and return an std::shared_ptr to the Item
    using allocate_function = std::function<void(Item&)>;

    /// The recycle function type
    /// If specified the recycle function will be called every time a
    /// resource gets recycled into the pool. This allows temporary
    /// resources, e.g., file handles to be closed when an object is longer
    /// used.
    using recycle_function = std::function<void(item_ptr)>;

public:
    /// Default constructor, we only want this to be available
    /// i.e. the boost_intrusive_pool to be default constructible if the
    /// Item we build is default constructible.
    ///
    /// This means that we only want
    /// std::is_default_constructible<boost_intrusive_pool<T>>::value to
    /// be true if the type T is default constructible.
    ///
    /// Unfortunately this does not work if we don't do the
    /// template magic seen below. What we do there is to use
    /// SFINAE to disable the default constructor for non default
    /// constructible types.
    ///
    /// It looks quite ugly and if somebody can fix in a simpler way
    /// please do :)
    boost_intrusive_pool(size_t init_size = BOOST_INTRUSIVE_POOL_DEFAULT_POOL_SIZE,
        size_t enlarge_size = BOOST_INTRUSIVE_POOL_INCREASE_STEP, recycle_function recycle = nullptr)
    {
        assert(init_size > 0);
        // assert(enlarge_size > 0); // NOTE: enlarge_size can be zero to create a limited-size memory pool

        // configurations
        // m_allocate_fn = nullptr;
        m_recycle_fn = recycle;
        m_enlarge_step = enlarge_size;
        m_last_free_item = nullptr;
        m_current_arena = nullptr;

        // stats
        m_free_count = 0;
        m_inuse_count = 0;
        m_total_count = 0;

        // do initial malloc
        enlarge(init_size);
    }
    virtual ~boost_intrusive_pool() { clear(); }

    /// Copy constructor
    boost_intrusive_pool(const boost_intrusive_pool& other) = delete;

    /// Move constructor
    boost_intrusive_pool(boost_intrusive_pool&& other) = delete;

    /// Copy assignment
    boost_intrusive_pool& operator=(const boost_intrusive_pool& other) = delete;

    /// Move assignment
    boost_intrusive_pool& operator=(boost_intrusive_pool&& other) = delete;

    item_ptr allocate()
    {
        if (m_free_count == 0) {
            assert(m_last_free_item == nullptr);
            if (m_enlarge_step == 0 || !enlarge(m_enlarge_step))
                return nullptr; // allocation by enlarge() failed or this is a fixed-size memory pool!
        }

        // get first item from free list
        assert(m_last_free_item != nullptr);
        Item* recycled_item = reinterpret_cast<Item*>(m_last_free_item); // upcast

        // update the pointer to the next free item available
        m_last_free_item = (Item*)recycled_item->_refcounted_item_get_next();

        // unlink the item to return
        recycled_item->_refcounted_item_set_next(nullptr);

        // update stats
        m_free_count--;
        m_inuse_count++;

        return recycled_item;
    }

    // Allocates an object in the current arena.
    // Uses C++11 perfect forwarding and C++ placement-new syntax
    template <typename... Args> item_ptr allocate_through_init(Args&&... args)
    {
        if (m_free_count == 0) {
            assert(m_last_free_item == nullptr);
            if (m_enlarge_step == 0 || !enlarge(m_enlarge_step))
                return nullptr; // allocation by enlarge() failed or this is a fixed-size memory pool!
        }

        // Get the first free item.
        assert(m_last_free_item != nullptr);
        Item* recycled_item = reinterpret_cast<Item*>(m_last_free_item); // upcast

        // Update the free list to the next free item.
        m_last_free_item = (Item*)recycled_item->_refcounted_item_get_next();

        // unlink the item to return
        recycled_item->_refcounted_item_set_next(nullptr);

        // Construct the object in the obtained storage
        // uses perfect forwarding and C++ placement-new
        recycled_item->init(std::forward<Args>(args)...);

        // update stats
        m_free_count--;
        m_inuse_count++;

        return recycled_item; //->m_allocate_fn(std::forward<Args>(args)...);
    }

    // Allocates an object in the current arena.
    // Uses C++11 perfect forwarding and C++ placement-new syntax
    template <typename... Args> item_ptr allocate_through_ctor(Args&&... args)
    {
        if (m_free_count == 0) {
            assert(m_last_free_item == nullptr);
            if (m_enlarge_step == 0 || !enlarge(m_enlarge_step))
                return nullptr; // allocation by enlarge() failed or this is a fixed-size memory pool!
        }

        // Get the first free item.
        assert(m_last_free_item != nullptr);
        Item* recycled_item = reinterpret_cast<Item*>(m_last_free_item); // upcast

        // Update the free list to the next free item.
        m_last_free_item = (Item*)recycled_item->_refcounted_item_get_next();

        // Construct the object in the obtained storage
        // uses perfect forwarding to the class ctor:
        new (recycled_item) Item(std::forward<Args>(args)...);

        // unlink the item to return; this step is actually not necessary: we just executed
        // the ctor of the recycled item; that resulted in a call to
        // boost_intrusive_pool_item::boost_intrusive_pool_item()!
        recycled_item->_refcounted_item_set_next(nullptr);

        // relinking the item to the pool is instead a critical step:
        recycled_item->_refcounted_item_set_pool(this);

        // update stats
        m_free_count--;
        m_inuse_count++;

        return recycled_item; //->m_allocate_fn(std::forward<Args>(args)...);
    }

    /*
        void free(T *t)
        {
                // Destroy the object.
                t->T::~T();

                // Convert this pointer to T to its enclosing pointer of an item of the
                // arena.
                minipool_item<Item> *current_item = minipool_item<Item>::storage_to_item(t);

                // Add the item at the beginning of the free list.
                current_item->set_next_item(free_list);
                free_list = current_item;
        }*/
    void clear()
    {
        boost_intrusive_pool_arena<Item>* pcurr = m_current_arena;
        while (pcurr) {
            boost_intrusive_pool_arena<Item>* pnext = pcurr->get_next_arena();
            delete pcurr;
            pcurr = pnext;
        }

        m_current_arena = nullptr;
        m_last_free_item = nullptr;
        m_free_count = 0;
        m_inuse_count = 0;
        m_total_count = 0;
    }

    // returns true if there are no elements in use.
    // Note that if empty()==true, it does not mean that capacity()==0 as well!
    bool empty() const { return m_free_count == m_total_count; }

    // returns the current (=maximum) capacity of the object pool
    size_t capacity() const { return m_total_count; }

    // returns the number of free entries of the pool
    size_t unused_count() const { return m_free_count; }

    // returns the number of items currently malloc()ed from this pool
    size_t inuse_count() const { return m_inuse_count; }

    // returns the number of mallocs done so far
    size_t enlarge_steps_done() const
    {
        size_t num_arenas_allocated = 0;

        boost_intrusive_pool_arena<Item>* pcurr = m_current_arena;
        while (pcurr) {
            pcurr = pcurr->get_next_arena();
            num_arenas_allocated++;
        }

        return num_arenas_allocated;
    }

private:
    bool enlarge(size_t arena_size)
    {
        // If the current arena is full, create a new one.
        boost_intrusive_pool_arena<Item>* new_arena = new boost_intrusive_pool_arena<Item>(arena_size, this);
        if (!new_arena)
            return false; // malloc failed... memory is over...

        // Link the new arena to the current one.
        if (m_current_arena)
            new_arena->set_next_arena(m_current_arena);
        // else
        // m_last_arena = new_arena;

        // Make the new arena the current one.
        m_current_arena = new_arena;

        // Update the free_list with the storage of the just created arena.
        m_last_free_item = m_current_arena->get_storage();

        m_free_count += arena_size;
        m_total_count += arena_size;

        return true;
    }

#if 0
	bool enlarge(size_t n)
	{
		assert(n > 0);

		// allocate a block of items
		Item* new_contiguous_block = new Item[n];
		if (!new_contiguous_block)
			return false; // malloc failed

		// insert new block in list
		m_node_list.push_back(new_contiguous_block);

#ifdef DEBUG
		ClassFactoryObject* baseClass = dynamic_cast<ClassFactoryObject*>(&x[0]);
		assert_msg(baseClass != nullptr, "The ITEM class is NOT derived from ClassFactoryObject?");
#endif

		// link together all items in this node
		for (size_t i = 0; i < n; ++i)
		{
			// point each item to the next item available in the free list (might be nullptr at the very beginning)
			new_contiguous_block[i]._refcounted_item_set_next(m_last_free_item);
			new_contiguous_block[i]._refcounted_item_set_pool(this);

			boost_intrusive_pool_item* pitem_base = &new_contiguous_block[i]; // downcast
			//boost::sp_adl_block::intrusive_ptr_add_ref<boost_intrusive_pool_item, boost::thread_unsafe_counter>(pitem_base);

			// update pointer to last-freed item available:
			m_last_free_item = &new_contiguous_block[i];
		}

		m_free_count+=n;
		m_total_count+=n;

		return true;
	}
#endif
    virtual void recycle(boost_intrusive_pool_item* pitem_base) override
    {
        BOOST_ASSERT_MSG(pitem_base && pitem_base->_refcounted_item_get_next() == nullptr,
            "Recycling an item that has been already recycled?");

        Item* pitem = (Item*)pitem_base; // upcast
        if (m_recycle_fn) {
            m_recycle_fn(pitem);
        } else {
            // Destroy the object using its dtor:
            pitem->Item::~Item();
        }

        // Convert this pointer to T to its enclosing pointer of an item of the
        // arena.
        // minipool_item<T> *current_item = minipool_item<T>::storage_to_item(t);

        // Add the item at the beginning of the free list.
        pitem_base->_refcounted_item_set_next(m_last_free_item);
        m_last_free_item = pitem_base;

        m_free_count++;

        assert(m_inuse_count > 0);
        m_inuse_count--;
    }

private:
    /// The recycle function
    recycle_function m_recycle_fn;

    /// How many new items to add each time the pool becomes full
    size_t m_enlarge_step;

    // Current arena. Changes when it becomes full and we want to allocate one
    // more object.
    boost_intrusive_pool_arena<Item>* m_current_arena;

    // List of free elements. The list can be threaded between different arenas
    // depending on the deallocation pattern.
    boost_intrusive_pool_item* m_last_free_item;

    // stats
    // This should hold always:
    //         m_free_count+m_inuse_count == m_total_count
    size_t m_free_count;
    size_t m_inuse_count;
    size_t m_total_count;
};

} // namespace memorypool
