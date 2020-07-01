#ifndef _XALLOCATOR_H
#define _XALLOCATOR_H

#include <stddef.h>
#include "DataTypes.h"
#include "Allocator.h"
#include "util.h"

// See
// http://www.codeproject.com/Articles/1084801/Replace-malloc-free-with-a-Fast-Fixed-Block-Memory

#ifdef __cplusplus

struct PoolListNode {
  Allocator *allocator;
  PoolListNode *next;
};

struct PoolListHeader {
  size_t capacity;
  PoolListNode *next;
};

struct XAllocState {
  /* The largest block size */
  size_t blocksize;
  /* Primary pool allocator, block size is set to the largest */
  Allocator primary;
  /* A pool of secondary allocators
   * The max number of secondary allocators is equal to the number of total
   * blocks in the primary allocator */
  Allocator secondaries;
  /* A pool of secondary allocator nodes
   * The max number of nodes is also equal to the number of blocks in the
   * primary allocator. */
  Allocator secondary_nodes;
  /* Headers of secondary allocator lists
   * This is an array of PoolListHeader objects created during xalloc_init().
   * Each PoolListHeader element connects a list of secondary pool nodes. Each
   * node represents a Secondary allocator whose block size = capacity.
   */
  size_t n_headers;
  PoolListHeader *secondary_headers;
};

// Define AUTOMATIC_XALLOCATOR_INIT_DESTROY to automatically call xalloc_init()
// and xalloc_destroy() when using xallocator in C++ projects. On embedded
// systems that never exit, you can save 1-byte of RAM storage per translation
// unit by undefining AUTOMATIC_XALLOCATOR_INIT_DESTROY and calling
// xalloc_init() manually before the OS starts. #define
// AUTOMATIC_XALLOCATOR_INIT_DESTROY
#ifdef AUTOMATIC_XALLOCATOR_INIT_DESTROY
/// If a C++ translation unit, create a static instance of XallocInitDestroy.
/// Any C++ file including xallocator.h will have the xallocDestroy instance
/// declared first within the translation unit and thus will be constructed
/// first. Destruction will occur in the reverse order so xallocInitDestroy is
/// called last. This way, any static user objects relying on xallocator will be
/// destroyed first before xalloc_destroy() is called. Embedded systems that
/// never exit can remove the XallocInitDestroy class entirely.
class XallocInitDestroy {
 public:
  XallocInitDestroy();
  ~XallocInitDestroy();

 private:
  static INT refCount;
};
static XallocInitDestroy xallocInitDestroy;
#endif  // AUTOMATIC_XALLOCATOR_INIT_DESTROY
#endif  // __cplusplus

#ifdef __cplusplus
extern "C" {
#endif

/* calc_required_memsize: Calculate total amounts of memory needed based on
 *   proposed data capacity and block size. Block size must be a power of 2
 *   and capacity must be a multiple of block size.
 *
 * @param[in] capacity:     Total data capacity
 * @param[in] blocksize:    The biggest block size
 *
 * @return: The total amount of memory required for this allocator.
 *          If it returns 0 then it decides that the parameters are invalid.
 */
static inline size_t calc_required_memsize(size_t capacity, size_t blocksize) {
  // Check if blocksize is a power of 2
  if ((blocksize & (blocksize - 1)) != 0) return 0;

  // Check if capacity is a multiple of blocksize
  if (capacity % blocksize != 0) return 0;

  size_t total = capacity;
  size_t nblocks = capacity / blocksize;
  /* Space for XAllocState (the primary allocator itself and the pool object of
   * secondaries) */
  total += sizeof(XAllocState);
  /* Space for potential secondary allocators */
  total += sizeof(Allocator) * nblocks;
  /* Space for secondary list nodes */
  total += sizeof(PoolListNode) * nblocks;
  /* Space for secondary list headers
   * We need log_2(blocksize / 8) headers
   */
  total += sizeof(PoolListHeader) * (BITS_TO_REPRESENT(blocksize) - 3);

  return total;
}

/// This function must be called exactly one time before the operating system
/// threading starts. If using xallocator exclusively in C files within your
/// application code, you must call this function before the OS starts. If using
/// C++, client code does not call xalloc_init. Let XallocInitDestroy() call
/// xalloc_init automatically. Embedded systems that never exit can call
/// xalloc_init() manually at startup and eliminate XallocInitDestroy usage.
/// When the system is still single threaded at startup, the xallocator API does
/// not need mutex protection.
#ifdef STATIC_POOLS
void xalloc_init();
#else
void xalloc_init(size_t data_capacity, size_t blocksize, void *memory);
#endif

/// This function must be called once when the application exits.  Never call
/// xalloc_destroy() manually except if using xallocator in a C-only
/// application. If using xallocator exclusively in C files within your
/// application code, you must call this function before the program exits. If
/// using C++, ~XallocInitDestroy() must call xalloc_destroy automatically.
/// Embedded systems that never exit need not call this function at all.
void xalloc_destroy();

/// Allocate a block of memory
/// @param[in] size - the size of the block to allocate.
void *xmalloc(size_t size);

/// Frees a previously xalloc allocated block
/// @param[in] ptr - a pointer to a previously allocated memory using xalloc.
void xfree(void *ptr);

/// Reallocates an existing xalloc block to a new size
/// @param[in] ptr - a pointer to a previously allocated memory using xalloc.
/// @param[in] size - the size of the new block
void *xrealloc(void *ptr, size_t size);

/// Output allocator statistics to the standard output
void xalloc_stats();

// Macro to overload new/delete with xalloc/xfree
#define XALLOCATOR                                          \
 public:                                                    \
  void *operator new(size_t size) { return xmalloc(size); } \
  void operator delete(void *pObject) { xfree(pObject); }

#ifdef __cplusplus
}
#endif

#endif
