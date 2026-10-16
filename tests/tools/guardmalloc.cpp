// guardmalloc.cpp - a guard-page allocator for tests, loaded with LD_PRELOAD.
//
// Every heap block ends at a page boundary followed by an inaccessible page, so
// reading or writing past the end of a block faults at once instead of
// corrupting a neighbour. Freed blocks are unmapped, so a use after free faults
// too (until the address is reused). Blocks are 16-byte aligned, which leaves up
// to 15 bytes after a size that is not a multiple of 16 unguarded. A block with
// a larger alignment leaves up to alignment - 1 bytes unguarded (valloc, about a
// page).
//
// The memcheck tests in tests/CMakeLists.txt run the suites that parse message
// data under it. It is slow and uses a page or more per allocation, so it is for
// tests only. free, realloc and malloc_usable_size abort on a pointer this
// allocator did not hand out.

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

namespace
{
    const uint64_t kMagic = 0x6775617264706721ULL;

    struct Header
    {
        uint64_t magic;     // marks a block this allocator handed out
        char * base;        // start of the mapping
        size_t mapLength;   // length of the mapping, including the guard page
        size_t size;        // size the caller asked for
    };

    size_t pageSize()
    {
        static const size_t page = (size_t)sysconf(_SC_PAGESIZE);
        return page;
    }

    bool isPowerOfTwo(size_t value)
    {
        return (value != 0) && ((value & (value - 1)) == 0);
    }

    [[noreturn]] void die(const char * message)
    {
        ssize_t ignored = write(2, message, strlen(message));
        (void)ignored;
        abort();
    }

    Header * headerOf(void * p)
    {
        Header * header = (Header *)((char *)p - sizeof(Header));
        if (header->magic != kMagic)
        {
            die("guardmalloc: pointer was not allocated by guardmalloc\n");
        }
        return header;
    }

    void * allocate(size_t size, size_t alignment)
    {
        if (alignment < 16)
        {
            alignment = 16;
        }
        size_t page = pageSize();
        size_t limit = SIZE_MAX - 15 - sizeof(Header) - 2 * page;
        if ((alignment > limit / 2) || (size > limit - alignment))
        {
            errno = ENOMEM;
            return NULL;
        }
        size_t usable = (size + 15) & ~(size_t)15;
        if (usable == 0)
        {
            usable = 16;
        }
        size_t dataPages = (usable + sizeof(Header) + alignment + page - 1) / page;
        size_t mapLength = (dataPages + 1) * page;
        char * base = (char *)mmap(NULL, mapLength, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (base == MAP_FAILED)
        {
            errno = ENOMEM;
            return NULL;
        }
        char * guard = base + dataPages * page;
        if (mprotect(guard, page, PROT_NONE) != 0)
        {
            munmap(base, mapLength);
            errno = ENOMEM;
            return NULL;
        }

        uintptr_t block = ((uintptr_t)guard - usable) & ~(uintptr_t)(alignment - 1);
        Header * header = (Header *)(block - sizeof(Header));
        header->magic = kMagic;
        header->base = base;
        header->mapLength = mapLength;
        header->size = size;
        return (void *)block;
    }

    void * allocateAligned(size_t alignment, size_t size)
    {
        if (!isPowerOfTwo(alignment))
        {
            errno = EINVAL;
            return NULL;
        }
        return allocate(size, alignment);
    }

    size_t roundUpToPage(size_t size)
    {
        size_t page = pageSize();
        if (size > SIZE_MAX - page)
        {
            return SIZE_MAX;        // allocate rejects it
        }
        size = (size + page - 1) & ~(page - 1);
        return (size == 0) ? page : size;
    }
}

extern "C"
{
    // Lets a test ask whether this allocator is loaded (dlsym).
    int rfh_guardmalloc_active(void)
    {
        return 1;
    }

    void * malloc(size_t size)
    {
        return allocate(size, 16);
    }

    void free(void * p)
    {
        if (p != NULL)
        {
            Header * header = headerOf(p);
            header->magic = 0;
            munmap(header->base, header->mapLength);
        }
    }

    void cfree(void * p)
    {
        free(p);
    }

    void free_sized(void * p, size_t)
    {
        free(p);
    }

    void free_aligned_sized(void * p, size_t, size_t)
    {
        free(p);
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
        size_t old = headerOf(p)->size;
        if (size == 0)
        {
            free(p);
            return NULL;
        }
        void * q = malloc(size);
        if (q != NULL)
        {
            memcpy(q, p, (old < size) ? old : size);
            free(p);
        }
        return q;
    }

    void * memalign(size_t alignment, size_t size)
    {
        return allocateAligned(alignment, size);
    }

    void * aligned_alloc(size_t alignment, size_t size)
    {
        return allocateAligned(alignment, size);
    }

    int posix_memalign(void ** result, size_t alignment, size_t size)
    {
        if (!isPowerOfTwo(alignment) || ((alignment % sizeof(void *)) != 0))
        {
            return EINVAL;
        }
        void * p = allocate(size, alignment);
        if (p == NULL)
        {
            return ENOMEM;
        }
        *result = p;
        return 0;
    }

    void * valloc(size_t size)
    {
        return allocate(size, pageSize());
    }

    void * pvalloc(size_t size)
    {
        return allocate(roundUpToPage(size), pageSize());
    }

    size_t malloc_usable_size(void * p)
    {
        return (p != NULL) ? headerOf(p)->size : 0;
    }
}
