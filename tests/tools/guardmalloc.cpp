// guardmalloc.cpp - a guard-page allocator for tests, loaded with LD_PRELOAD.
//
// Every heap block ends at a page boundary and is followed by an inaccessible
// page, so reading or writing past the end of a block faults at once instead of
// corrupting a neighbour. Freed blocks are unmapped, so a use after free faults
// too (until the address is reused). Blocks are 16-byte aligned, which leaves up
// to 15 bytes after a size that is not a multiple of 16 unguarded.
//
// The memcheck tests in tests/CMakeLists.txt run the suites that parse message
// data under it. It is slow and uses a page or more per allocation, so it is for
// tests only.

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

namespace
{
    struct Header
    {
        char * base;        // start of the mapping
        size_t mapLength;   // length of the mapping, including the guard page
        size_t size;        // size the caller asked for
    };

    size_t pageSize()
    {
        static size_t page = 0;
        if (page == 0)
        {
            page = (size_t)sysconf(_SC_PAGESIZE);
        }
        return page;
    }

    Header * headerOf(void * p)
    {
        return (Header *)((char *)p - sizeof(Header));
    }

    void * allocate(size_t size, size_t alignment)
    {
        if (alignment < 16)
        {
            alignment = 16;
        }
        size_t usable = (size + 15) & ~(size_t)15;
        if (usable == 0)
        {
            usable = 16;
        }
        size_t page = pageSize();
        size_t dataPages = (usable + sizeof(Header) + alignment + page - 1) / page;
        size_t mapLength = (dataPages + 1) * page;
        char * base = (char *)mmap(NULL, mapLength, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (base == MAP_FAILED)
        {
            errno = ENOMEM;
            return NULL;
        }
        char * guard = base + dataPages * page;
        mprotect(guard, page, PROT_NONE);

        uintptr_t block = ((uintptr_t)guard - usable) & ~(uintptr_t)(alignment - 1);
        Header * header = headerOf((void *)block);
        header->base = base;
        header->mapLength = mapLength;
        header->size = size;
        return (void *)block;
    }
}

extern "C"
{
    void * malloc(size_t size)
    {
        return allocate(size, 16);
    }

    void free(void * p)
    {
        if (p != NULL)
        {
            Header * header = headerOf(p);
            munmap(header->base, header->mapLength);
        }
    }

    void * calloc(size_t count, size_t size)
    {
        if ((size != 0) && (count > SIZE_MAX / size))
        {
            errno = ENOMEM;
            return NULL;
        }
        return allocate(count * size, 16);      // mmap memory is already zero
    }

    void * realloc(void * p, size_t size)
    {
        if (p == NULL)
        {
            return malloc(size);
        }
        void * q = malloc(size);
        if (q != NULL)
        {
            size_t old = headerOf(p)->size;
            memcpy(q, p, (old < size) ? old : size);
            free(p);
        }
        return q;
    }

    void * memalign(size_t alignment, size_t size)
    {
        return allocate(size, alignment);
    }

    void * aligned_alloc(size_t alignment, size_t size)
    {
        return allocate(size, alignment);
    }

    int posix_memalign(void ** result, size_t alignment, size_t size)
    {
        *result = allocate(size, alignment);
        return (*result != NULL) ? 0 : ENOMEM;
    }

    void * valloc(size_t size)
    {
        return allocate(size, pageSize());
    }

    size_t malloc_usable_size(void * p)
    {
        return (p != NULL) ? headerOf(p)->size : 0;
    }
}
