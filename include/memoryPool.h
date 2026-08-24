/******************************************************************************
 * @file    memoryPool.h
 * @brief   Public interface for a fixed-size memory pool allocator.
 *
 * @author  Abhishek Doddagoudar
 * @date    August 2026
 *
 * @details
 * Implements a runtime-configurable, fixed-size block allocator suitable for
 * embedded systems. Block size and block count are chosen by the caller at
 * initialization; once initialization succeeds, that configuration is fixed
 * for the pool's lifetime.
 *
 * See docs/memoryPool-design.md for the full design rationale, in
 * particular:
 *  - Section 2: why blocks are found via an explicit free stack and
 *    returned as raw pointers, rather than via a generation-tagged handle
 *    found by linear scan.
 *  - Section 6: why pointer validation on release is expressed with
 *    uintptr_t arithmetic, and why a true double free cannot always be
 *    distinguished from a pointer that was never actually acquired.
 *  - Section 7: the alignment contract storage and blockSize must satisfy.
 *  - Section 8: the initialized-flag mechanism, and why memoryPoolInit()
 *    now rejects a pool that is already initialized instead of silently
 *    resetting it.
 *
 * Features:
 *  - No dynamic memory allocation -- all storage is caller-owned
 *  - O(1) acquire and release, independent of pool size or fill level
 *  - No external fragmentation (fixed block size)
 *  - Free-stack allocation strategy (LIFO reuse order)
 *  - Explicit initialization-state tracking: distinguishes a NULL pool
 *    pointer from an un-initialized pool, and rejects re-initialization
 *    of a pool that is already initialized
 *  - Detects pool exhaustion and certain classes of misuse on release:
 *    foreign pointers, misaligned pointers, and already-free blocks
 *  - Reentrant
 *
 * Not Supported (by design -- see docs/memoryPool-design.md):
 *  - Variable block sizes within one pool instance
 *  - Growing or shrinking a pool after initialization
 *  - Resetting an already-initialized pool back to uninitialized --
 *    memoryPoolInit() rejects a second call outright (Section 8); there
 *    is no separate "deinit" or "reset" operation
 *  - Distinguishing a true double free from a coincidentally-valid pointer
 *    that was never actually acquired -- both are reported as
 *    MEMORY_POOL_ERROR_INVALID_BLOCK (Section 6)
 *  - Detecting use-after-free
 *  - Internal synchronization -- concurrent access to the same instance is
 *    the caller's responsibility
 *
 * Thread Safety
 *  - This module is reentrant.
 *  - Concurrent access to the same pool instance must be synchronized by the
 *    caller. Acquire and release are each multi-step operations internally;
 *    an interruption mid-operation on a shared instance can corrupt pool
 *    bookkeeping, not just return a stale value.
 *
 * Caller Contract
 *  - storage, state, and freeStack (see MemoryPool_t) must refer to three
 *    genuinely separate, non-overlapping regions of memory, each sized for
 *    at least what is passed to memoryPoolInit().
 *  - storage must be suitably aligned, and blockSize must be a multiple of
 *    that alignment, for whatever type the caller intends to store in the
 *    pool -- this module does not check or correct for alignment.
 *  - memoryPoolInit() may only be called once per MemoryPool_t instance.
 *    A second call returns MEMORY_POOL_ERROR_ALREADY_INITIALIZED and
 *    changes nothing about the pool's existing state.
 *
 * Example Usage:
 *
 *      #define BLOCK_SIZE  32
 *      #define BLOCK_COUNT 8
 *
 *      static uint8_t storage[BLOCK_SIZE * BLOCK_COUNT];
 *      static uint8_t state[BLOCK_COUNT];
 *      static size_t  freeStack[BLOCK_COUNT];
 *      MemoryPool_t pool;
 *
 *      memoryPoolInit(&pool, storage, state, freeStack, BLOCK_SIZE, BLOCK_COUNT);
 *
 *      void *block;
 *      memoryPoolAcquire(&pool, &block);
 *      // use block
 *      memoryPoolRelease(&pool, block);
 *
 ******************************************************************************/

#ifndef MEMORY_POOL_H
#define MEMORY_POOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Per-block allocation state.
 *
 * @details
 * Defined for symbolic clarity in documentation and implementation. The
 * MemoryPool_t::state array stores these values as uint8_t to keep metadata
 * size minimal; callers should not read or write state entries directly.
 */
typedef enum
{
    MEMORY_POOL_BLOCK_FREE = 0,
    MEMORY_POOL_BLOCK_ALLOCATED
} MemoryPoolBlockState_t;

/**
 * @brief Memory pool object.
 *
 * The caller owns this object and all three backing regions it points to:
 * storage, state, and freeStack. The pool performs no dynamic allocation of
 * its own at any point. Fields should be treated as opaque and manipulated
 * only through the API below; direct field access is not part of the
 * public contract and may change between versions.
 */
typedef struct
{
    uint8_t *storage;  /**< Caller-owned block storage: blockSize * blockCount bytes.        */
    uint8_t *state;    /**< Caller-owned, blockCount entries. Values are encoded using MemoryPoolBlockState_t. */
    size_t *freeStack; /**< Caller-owned, blockCount entries. LIFO stack of free block indices. */
    size_t blockSize;  /**< Size of one block in bytes, fixed after Init.                     */
    size_t blockCount; /**< Total number of blocks, fixed after Init.                         */
    size_t freeCount;  /**< Number of blocks currently free; also the free-stack height.      */
    bool initialized;  /**< true only after a successful memoryPoolInit(); see design doc Section 8. */
} MemoryPool_t;

/**
 * @brief Memory pool status codes.
 */
typedef enum
{
    MEMORY_POOL_SUCCESS = 0,
    MEMORY_POOL_ERROR_NULL_POINTER,
    MEMORY_POOL_ERROR_NOT_INITIALIZED,
    MEMORY_POOL_ERROR_ALREADY_INITIALIZED,
    MEMORY_POOL_ERROR_INVALID_BLOCK_SIZE,
    MEMORY_POOL_ERROR_INVALID_BLOCK_COUNT,
    MEMORY_POOL_ERROR_SIZE_OVERFLOW,
    MEMORY_POOL_ERROR_POOL_FULL,
    MEMORY_POOL_ERROR_INVALID_BLOCK
} MemoryPoolStatus_t;

/**
 * @brief Initialize a memory pool.
 *
 * @details This function initializes a memory pool with the specified parameters.
 *
 * @param pool        Memory pool object. Does not need to be zero-
 *                    initialized beforehand; Init sets every field this
 *                    module relies on, including initialized.
 * @param storage     Caller-provided block storage, blockSize * blockCount bytes.
 * @param state       Caller-provided per-block state array, blockCount entries.
 * @param freeStack   Caller-provided free-block index stack, blockCount entries.
 * @param blockSize   Size of one block in bytes. Must be nonzero.
 * @param blockCount  Number of blocks. Must be nonzero.
 *
 * @return MemoryPoolStatus_t Status of the operation.
 *
 * @retval MEMORY_POOL_SUCCESS Initialization successful; pool is ready to
 *         use with all blockCount blocks free.
 * @retval MEMORY_POOL_ERROR_NULL_POINTER pool, storage, state, or
 *         freeStack is NULL.
 * @retval MEMORY_POOL_ERROR_ALREADY_INITIALIZED pool->initialized is
 *         already true; pool's existing state is left unchanged.
 * @retval MEMORY_POOL_ERROR_INVALID_BLOCK_SIZE blockSize is 0.
 * @retval MEMORY_POOL_ERROR_INVALID_BLOCK_COUNT blockCount is 0.
 * @retval MEMORY_POOL_ERROR_SIZE_OVERFLOW blockSize * blockCount would
 *         overflow size_t.
 */
MemoryPoolStatus_t memoryPoolInit(MemoryPool_t *pool, void *storage, uint8_t *state, size_t *freeStack, size_t blockSize, size_t blockCount);

/**
 * @brief Acquire one block from the pool.
 *
 * @details  If the pool is empty, *block is left unchanged.
 *
 * @param pool   Memory pool, previously initialized by memoryPoolInit().
 * @param block  Destination for the acquired block's address.
 *
 * @return MemoryPoolStatus_t Status of the operation.
 *
 * @retval MEMORY_POOL_SUCCESS *block now points at a usable block.
 * @retval MEMORY_POOL_ERROR_NULL_POINTER pool or block is NULL.
 * @retval MEMORY_POOL_ERROR_NOT_INITIALIZED pool->initialized is not true.
 * @retval MEMORY_POOL_ERROR_POOL_FULL No blocks are currently free;
 *         *block is left unchanged.
 */
MemoryPoolStatus_t memoryPoolAcquire(MemoryPool_t *pool, void **block);

/**
 * @brief Release a previously acquired block back to the pool.
 *
 * @details  The caller must not use the block after releasing it; doing so is
 *         undefined behavior.
 *
 * @param pool   Memory pool, previously initialized by memoryPoolInit().
 * @param block  Block previously returned by memoryPoolAcquire() on this
 *               same pool instance.
 *
 * @return MemoryPoolStatus_t Status of the operation.
 *
 * @retval MEMORY_POOL_SUCCESS block has been returned to the pool.
 * @retval MEMORY_POOL_ERROR_NULL_POINTER pool is NULL.
 * @retval MEMORY_POOL_ERROR_NOT_INITIALIZED pool->initialized is not true.
 * @retval MEMORY_POOL_ERROR_INVALID_BLOCK block is NULL, does not fall
 *         within this pool's storage region, does not land on a block
 *         boundary, or is already marked free.
 */
MemoryPoolStatus_t memoryPoolRelease(MemoryPool_t *pool, void *block);

/**
 * @brief Get the number of blocks currently free.
 *
 * @details  Reflects the pool's state at the time of the call; the value is a
 *         snapshot and may become stale immediately if another Acquire() or
 *         Release() occurs afterward (this module provides no internal locking --
 *         see Thread Safety).
 *
 * @param pool       Memory pool, previously initialized by memoryPoolInit().
 * @param freeCount  Number of blocks currently available to acquire.
 *
 * @return MemoryPoolStatus_t Status of the operation.
 *
 * @retval MEMORY_POOL_SUCCESS *freeCount has been populated.
 * @retval MEMORY_POOL_ERROR_NULL_POINTER pool or freeCount is NULL.
 * @retval MEMORY_POOL_ERROR_NOT_INITIALIZED pool->initialized is not true.
 */
MemoryPoolStatus_t memoryPoolGetFreeCount(const MemoryPool_t *pool, size_t *freeCount);

/**
 * @brief Get the number of blocks currently acquired.
 *
 * @details  Equal to blockCount - freeCount at the time of the call; the same
 *         snapshot caveat as memoryPoolGetFreeCount() applies.
 *
 * @param pool       Memory pool, previously initialized by memoryPoolInit().
 * @param usedCount  Number of blocks currently checked out.
 *
 * @return MemoryPoolStatus_t Status of the operation.
 *
 * @retval MEMORY_POOL_SUCCESS *usedCount has been populated.
 * @retval MEMORY_POOL_ERROR_NULL_POINTER pool or usedCount is NULL.
 * @retval MEMORY_POOL_ERROR_NOT_INITIALIZED pool->initialized is not true.
 */
MemoryPoolStatus_t memoryPoolGetUsedCount(const MemoryPool_t *pool, size_t *usedCount);

/**
 * @brief Get the total block capacity of the pool.
 *
 * @details  Equal to blockCount; the same snapshot caveat as memoryPoolGetFreeCount() applies.
 *
 * @param pool      Memory pool, previously initialized by memoryPoolInit().
 * @param capacity  Total number of blocks the pool was initialized with.
 *
 * @return MemoryPoolStatus_t Status of the operation.
 *
 * @retval MEMORY_POOL_SUCCESS *capacity has been populated.
 * @retval MEMORY_POOL_ERROR_NULL_POINTER pool or capacity is NULL.
 * @retval MEMORY_POOL_ERROR_NOT_INITIALIZED pool->initialized is not true.
 */
MemoryPoolStatus_t memoryPoolGetCapacity(const MemoryPool_t *pool, size_t *capacity);

/**
 * @brief Get the size of one block, in bytes.
 *
 * @details  Equal to blockSize as passed to memoryPoolInit(); fixed for the
 *         pool's lifetime.
 *
 * @param pool       Memory pool, previously initialized by memoryPoolInit().
 * @param blockSize  Size of one block, in bytes.
 *
 * @return MemoryPoolStatus_t Status of the operation.
 *
 * @retval MEMORY_POOL_SUCCESS *blockSize has been populated.
 * @retval MEMORY_POOL_ERROR_NULL_POINTER pool or blockSize is NULL.
 * @retval MEMORY_POOL_ERROR_NOT_INITIALIZED pool->initialized is not true.
 */
MemoryPoolStatus_t memoryPoolGetBlockSize(const MemoryPool_t *pool, size_t *blockSize);

#endif /* MEMORY_POOL_H */

/******************************* END OF FILE **********************************/
