#pragma once
#include <pthread.h>

#include <iostream>

#define TRACE_METHOD() std::cout << "[Executing " << __PRETTY_FUNCTION__ << " for instance=" << this << "]\n";

//------------------------------------------------------------------------------
// Utility functions to trace mallocs
//------------------------------------------------------------------------------

// replace operator new and delete to log allocations
void* operator new(std::size_t n)
{
    void* ret = malloc(n);
    std::cout << "[Allocating " << n << "bytes: " << ret << "]" << std::endl;
    return ret;
}

void operator delete(void* p) throw()
{
    std::cout << "[Freeing " << p << "]" << std::endl;
    free(p);
}

void operator delete(void* p, size_t n) throw()
{
    std::cout << "[Freeing " << p << "]" << std::endl;
    free(p);
}

void print_header()
{
    std::cout << "**************************************************************************************" << std::endl;
}
