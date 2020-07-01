#include "xallocator.h"
#include "Allocator.h"
#include "Fault.h"
#include <iostream>
#include <mutex>
#include <cstring>

using namespace std;

#ifndef CHAR_BIT
#define CHAR_BIT	8 
#endif

// static CRITICAL_SECTION _criticalSection; 
static std::mutex _criticalSection; 
static BOOL _xallocInitialized = FALSE;

XAllocState *my;
void *memory;

// Define STATIC_POOLS to switch from heap blocks mode to static pools mode
#define STATIC_POOLS 
#ifdef STATIC_POOLS
	// Update this section as necessary if you want to use static memory pools.
	// See also xalloc_init() and xalloc_destroy() for additional updates required.
	/* Maximum block size, defaults to 4096 (unit: bytes) */
	#ifndef BLOCKSZ
	#define BLOCKSZ		4096
	#endif

	/* Capacity of data region, defaults to 16384 (unit: Kbytes) */
	#ifndef DATA_CAP
	#define DATA_CAP	16384
	#endif

	/* Validate BLOCKSZ and DATA_CAP */
	#if DATA_CAP % BLOCKSZ != 0
	#error DATA_CAP must be a multiple of BLOCKSZ
	#endif
	#if (BLOCKSZ & (BLOCKSZ - 1)) != 0
	#error BLOCKSZ must be a power of 2
	#endif

	// Create static storage for each static allocator instance
	/* Calculate total pool size */
	#define NBLOCKS		(DATA_CAP * 1024 / BLOCKSZ)
	/* Headers are for 8, 16, 32, 64, ..., BLOCKSZ/2 */
	#define NHEADERS	(BITS_TO_REPRESENT(BLOCKSZ) - 3)
	#define POOL_SIZE	DATA_CAP * 1024 \
				+ sizeof(Allocator) * NBLOCKS \
				+ sizeof(PoolListNode) * NBLOCKS \
				+ sizeof(PoolListHeader) * NHEADERS

	CHAR _static_pool [POOL_SIZE];
	XAllocState _static_xalloc_state;

#endif	// STATIC_POOLS

// For C++ applications, must define AUTOMATIC_XALLOCATOR_INIT_DESTROY to 
// correctly ensure allocators are initialized before any static user C++ 
// construtor/destructor executes which might call into the xallocator API. 
// This feature costs 1-byte of RAM per C++ translation unit. This feature 
// can be disabled only under the following circumstances:
// 
// 1) The xallocator is only used within C files. 
// 2) STATIC_POOLS is undefined and the application never exits main (e.g. 
// an embedded system). 
//
// In either of the two cases above, call xalloc_init() in main at startup, 
// and xalloc_destroy() before main exits. In all other situations
// XallocInitDestroy must be used to call xalloc_init() and xalloc_destroy().
#ifdef AUTOMATIC_XALLOCATOR_INIT_DESTROY
INT XallocInitDestroy::refCount = 0;
XallocInitDestroy::XallocInitDestroy() 
{ 
	// Track how many static instances of XallocInitDestroy are created
	if (refCount++ == 0)
		xalloc_init();
}

XallocInitDestroy::~XallocInitDestroy()
{
	// Last static instance to have destructor called?
	if (--refCount == 0)
		xalloc_destroy();
}
#endif	// AUTOMATIC_XALLOCATOR_INIT_DESTROY

/// Returns the next higher powers of two. For instance, pass in 12 and 
/// the value returned would be 16. 
/// @param[in] k - numeric value to compute the next higher power of two.
/// @return	The next higher power of two based on the input k. 
template <class T>
T nexthigher(T k) 
{
    k--;
    for (size_t i=1; i<sizeof(T)*CHAR_BIT; i<<=1)
        k |= (k >> i);
    return k+1;
}

/// Create the xallocator lock. Call only one time at startup. 
static void lock_init()
{
	_xallocInitialized = TRUE;
}

/// Destroy the xallocator lock.
static void lock_destroy()
{
	_xallocInitialized = FALSE;
}

/// Lock the shared resource. 
static inline void lock_get()
{
	if (_xallocInitialized == FALSE)
		return;

	_criticalSection.lock(); 
}

/// Unlock the shared resource. 
static inline void lock_release()
{
	if (_xallocInitialized == FALSE)
		return;

	_criticalSection.unlock();
}

/// Stored a pointer to the allocator instance within the block region. 
///	a pointer to the client's area within the block.
/// @param[in] block - a pointer to the raw memory block. 
///	@param[in] size - the client requested size of the memory block.
/// @return	A pointer to the client's address within the raw memory block. 
static inline void *set_block_allocator(void* block, Allocator* allocator)
{
	// Cast the raw block memory to a Allocator pointer
	Allocator** pAllocatorInBlock = static_cast<Allocator**>(block);

	// Write the size into the memory block
	*pAllocatorInBlock = allocator;

	// Advance the pointer past the Allocator* block size and return a pointer to
	// the client's memory region
	return ++pAllocatorInBlock;
}

/// Gets the size of the memory block stored within the block.
/// @param[in] block - a pointer to the client's memory block. 
/// @return	The original allocator instance stored in the memory block.
static inline Allocator* get_block_allocator(void* block)
{
	// Cast the client memory to a Allocator pointer
	Allocator** pAllocatorInBlock = static_cast<Allocator**>(block);

	// Back up one Allocator* position to get the stored allocator instance
	pAllocatorInBlock--;

	// Return the allocator instance stored within the memory block
	return *pAllocatorInBlock;
}

/// Returns the raw memory block pointer given a client memory pointer. 
/// @param[in] block - a pointer to the client memory block. 
/// @return	A pointer to the original raw memory block address. 
static inline void *get_block_ptr(void* block)
{
	// Cast the client memory to a Allocator* pointer
	Allocator** pAllocatorInBlock = static_cast<Allocator**>(block);

	// Back up one Allocator* position and return the original raw memory block pointer
	return --pAllocatorInBlock;
}

/// Returns an allocator instance matching the size provided
/// @param[in] size - allocator block size
/// @return Allocator instance handling requested block size or NULL
/// if no allocator exists. 
static inline Allocator* find_allocator(size_t size)
{
	for (INT i=0; i<MAX_ALLOCATORS; i++)
	{
		if (_allocators[i] == 0)
			break;
		
		if (_allocators[i]->GetBlockSize() == size)
			return _allocators[i];
	}
	
	return NULL;
}

/// Insert an allocator instance into the array
/// @param[in] allocator - An allocator instance
static inline void insert_allocator(Allocator* allocator)
{
	for (INT i=0; i<MAX_ALLOCATORS; i++)
	{
		if (_allocators[i] == 0)
		{
			_allocators[i] = allocator;
			return;
		}
	}
	
	ASSERT();
}

/// This function must be called exactly one time *before* any other xallocator
/// API is called. XallocInitDestroy constructor calls this function automatically. 
#ifdef STATIC_POOLS
extern "C" void xalloc_init()
{
	lock_init();
	memory = _static_pool;
	size_t data_capacity = DATA_CAP * 1024;
	size_t blocksize = BLOCKSZ;
	
	char *datapool = memory;
	my = new (&_static_xalloc_state) XAllocState();
#else
extern "C" void xalloc_init(size_t data_capacity, size_t blocksize, void *user_pool)
{
	lock_init();
	memory = user_pool;
	char *datapool = memory + sizeof(Allocator);
	my = new (user_pool) XAllocState();
#endif

	size_t nblocks = data_capacity / blocksize;
	/* Initialize primary allocator */
	my->primary = Allocator(blocksize, nblocks, datapool,
				"Primary allocator");
	/* Secondaries */
	char *secondary_pool = datapool + data_capacity;
	my->secondaries = Allocator(sizeof(Allocator), nblocks, secondary_pool,
				    "Allocator of secondary allocators");
	char *sec_nodes_pool = secondary_pool + sizeof(Allocator) * nblocks;
	my->secondary_nodes = Allocator(sizeof(PoolListNode), nblocks,
		sec_nodes_pool, "Allocators for secondary list nodes");
	my->n_headers = BITS_TO_REPRESENT(blocksize) - 3;
	my->secondary_headers = sec_nodes_pool + sizeof(PoolListNode) * nblocks;
	for (int i = 0; i < my->n_headers; ++i) {
		my->secondary_headers[i].capacity = (8 << i);
		my->secondary_headers[i].next = nullptr;
	}
}

/// Called one time when the application exits to cleanup any allocated memory.
/// ~XallocInitDestroy destructor calls this function automatically. 
extern "C" void xalloc_destroy()
{
	/* Let's not do anything here due to the following consideration:
	 * 1. When the whole process exits the kernel will reclaim all the memory
	 *    anyway. This apply to both static pool and user-provided pool.
	 * 2. If the pool is user provided, we can't just call free() or delete
	 *    on it because the pool may also be mmap'ed.
	 */ 

	lock_destroy();
}

/// Get an Allocator instance based upon the client's requested block size.
/// If a Allocator instance is not currently available to handle the size,
///	then a new Allocator instance is create.
///	@param[in] size - the client's requested block size.
///	@return An Allocator instance that handles blocks of the requested
///	size.
extern "C" Allocator* xallocator_get_allocator(size_t size)
{
	// Based on the size, find the next higher powers of two value.
	// Add sizeof(Allocator*) to the requested block size to hold the size
	// within the block memory region. Most blocks are powers of two,
	// however some common allocator block sizes can be explicitly defined
	// to minimize wasted storage. This offers application specific tuning.
	size_t blockSize = size + sizeof(Allocator*);
	if (blockSize > 256 && blockSize <= 396)
		blockSize = 396;
	else if (blockSize > 512 && blockSize <= 768)
		blockSize = 768;
	else
		blockSize = nexthigher<size_t>(blockSize);

	Allocator* allocator = find_allocator(blockSize);

#ifdef STATIC_POOLS
	ASSERT_TRUE(allocator != NULL);
#else
	// If there is not an allocator already created to handle this block size
	if (allocator == NULL)  
	{
		// Create a new allocator to handle blocks of the size required
		allocator = new Allocator(blockSize, 0, 0, "xallocator");

		// Insert allocator into array
		insert_allocator(allocator);
	}
#endif
	
	return allocator;
}

/// Allocates a memory block of the requested size. The blocks are created from
///	the fixed block allocators.
///	@param[in] size - the client requested size of the block.
/// @return	A pointer to the client's memory block.
extern "C" void *xmalloc(size_t size)
{
	lock_get();

	// Allocate a raw memory block 
	Allocator* allocator = xallocator_get_allocator(size);
	void* blockMemoryPtr = allocator->Allocate(sizeof(Allocator*) + size);

	lock_release();

	// Set the block Allocator* within the raw memory block region
	void* clientsMemoryPtr = set_block_allocator(blockMemoryPtr, allocator);
	return clientsMemoryPtr;
}

/// Frees a memory block previously allocated with xalloc. The blocks are returned
///	to the fixed block allocator that originally created it.
///	@param[in] ptr - a pointer to a block created with xalloc.
extern "C" void xfree(void* ptr)
{
	if (ptr == 0)
		return;

	// Extract the original allocator instance from the caller's block pointer
	Allocator* allocator = get_block_allocator(ptr);

	// Convert the client pointer into the original raw block pointer
	void* blockPtr = get_block_ptr(ptr);

	lock_get();

	// Deallocate the block 
	allocator->Deallocate(blockPtr);

	lock_release();
}

/// Reallocates a memory block previously allocated with xalloc.
///	@param[in] ptr - a pointer to a block created with xalloc.
///	@param[in] size - the client requested block size to create.
extern "C" void *xrealloc(void *oldMem, size_t size)
{
	if (oldMem == 0)
		return xmalloc(size);

	if (size == 0) 
	{
		xfree(oldMem);
		return 0;
	}
	else 
	{
		// Create a new memory block
		void* newMem = xmalloc(size);
		if (newMem != 0) 
		{
			// Get the original allocator instance from the old memory block
			Allocator* oldAllocator = get_block_allocator(oldMem);
			size_t oldSize = oldAllocator->GetBlockSize() - sizeof(Allocator*);

			// Copy the bytes from the old memory block into the new (as much as will fit)
			memcpy(newMem, oldMem, (oldSize < size) ? oldSize : size);

			// Free the old memory block
			xfree(oldMem);

			// Return the client pointer to the new memory block
			return newMem;
		}
		return 0;
	}
}

/// Output xallocator usage statistics
extern "C" void xalloc_stats()
{
	lock_get();

	for (INT i=0; i<MAX_ALLOCATORS; i++)
	{
		if (_allocators[i] == 0)
			break;

		if (_allocators[i]->GetName() != NULL)
			cout << _allocators[i]->GetName();
		cout << " Block Size: " << _allocators[i]->GetBlockSize();
		cout << " Block Count: " << _allocators[i]->GetBlockCount();
		cout << " Blocks In Use: " << _allocators[i]->GetBlocksInUse();
		cout << endl;
	}

	lock_release();
}


