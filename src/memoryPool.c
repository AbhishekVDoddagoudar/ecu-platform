/******************************************************************************
 * @file   memoryPool.c
 * @brief  Implementation of the reentrant, caller-owned fixed-size memory pool.
 *
 * @author Abhishek Doddagoudar
 * @date   August 2026
 ******************************************************************************/

#include <memoryPool.h>

/**
 * @internal
 * @brief Validates a pool pointer and its initialization state.
 *
 * @details Checks: pool is non-NULL, and pool->initialized is true. Every
 *          public API below other than memoryPoolInit() itself calls this
 *          first, before touching storage, state, or freeStack -- see
 *          design doc Section 8. Does not check pool->storage/state/
 *          freeStack individually; a pool for which initialized is true
 *          was only ever set that way by a successful memoryPoolInit(),
 *          which is the sole place those fields are populated.
 *
 * @param pool  Pool to validate.
 *
 * @return MemoryPoolStatus_t
 * @retval MEMORY_POOL_SUCCESS pool is non-NULL and initialized.
 * @retval MEMORY_POOL_ERROR_NULL_POINTER pool is NULL.
 * @retval MEMORY_POOL_ERROR_NOT_INITIALIZED pool->initialized is not true.
 */
static MemoryPoolStatus_t validatePool(const MemoryPool_t *pool)
{
    if (pool == NULL)
    {
        return MEMORY_POOL_ERROR_NULL_POINTER;
    }

    if (!pool->initialized)
    {
        return MEMORY_POOL_ERROR_NOT_INITIALIZED;
    }

    return MEMORY_POOL_SUCCESS;
}

/**
 * @brief Initialize a memory pool.
 *
 * @details Order of checks matters here. NULL pointers are rejected first;
 *          pool->initialized is checked next (before blockSize/blockCount
 *          are even looked at) so that calling Init a second time is
 *          rejected outright without re-validating parameters that are, by
 *          definition, not going to be used. blockSize and blockCount are
 *          then validated individually, in that order, before the overflow
 *          guard -- blockCount must already be known nonzero by the time
 *          the overflow guard divides by it (Section 8). initialized is
 *          set true only as the very last step, after state[] and
 *          freeStack[] have both been fully populated: if anything above
 *          this point had failed, the pool must never claim to be
 *          initialized.
 *
 * @param pool        Pointer to the pool object to initialize.
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
MemoryPoolStatus_t memoryPoolInit(MemoryPool_t *pool, void *storage, uint8_t *state,
                                   size_t *freeStack, size_t blockSize, size_t blockCount)
{
    size_t i;

    if ((pool == NULL) || (storage == NULL) || (state == NULL) || (freeStack == NULL))
    {
        return MEMORY_POOL_ERROR_NULL_POINTER;
    }

    if (pool->initialized)
    {
        return MEMORY_POOL_ERROR_ALREADY_INITIALIZED;
    }

    if (blockSize == 0U)
    {
        return MEMORY_POOL_ERROR_INVALID_BLOCK_SIZE;
    }

    if (blockCount == 0U)
    {
        return MEMORY_POOL_ERROR_INVALID_BLOCK_COUNT;
    }

    /* blockCount is already known nonzero above, so this division is safe. */
    if (blockSize > (SIZE_MAX / blockCount))
    {
        return MEMORY_POOL_ERROR_SIZE_OVERFLOW;
    }

    for (i = 0U; i < blockCount; i++)
    {
        state[i] = (uint8_t)MEMORY_POOL_BLOCK_FREE;
        freeStack[i] = i;
    }

    pool->storage = (uint8_t *)storage;
    pool->state = state;
    pool->freeStack = freeStack;
    pool->blockSize = blockSize;
    pool->blockCount = blockCount;
    pool->freeCount = blockCount;
    pool->initialized = true;

    return MEMORY_POOL_SUCCESS;
}

/**
 * @brief Acquire one block from the pool.
 *
 * @details O(1): pops one index off freeStack, marks it allocated in
 *          state[], and converts the index to an address. block is
 *          checked for NULL before the pool itself, matching this
 *          module's other output-pointer parameters. See design doc
 *          Section 5.
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
MemoryPoolStatus_t memoryPoolAcquire(MemoryPool_t *pool, void **block)
{
    MemoryPoolStatus_t status;
    size_t index;

    if (block == NULL)
    {
        return MEMORY_POOL_ERROR_NULL_POINTER;
    }

    status = validatePool(pool);
    if (status != MEMORY_POOL_SUCCESS)
    {
        return status;
    }

    if (pool->freeCount == 0U)
    {
        return MEMORY_POOL_ERROR_POOL_FULL;
    }

    pool->freeCount--;
    index = pool->freeStack[pool->freeCount];
    pool->state[index] = (uint8_t)MEMORY_POOL_BLOCK_ALLOCATED;

    *block = &pool->storage[index * pool->blockSize];

    return MEMORY_POOL_SUCCESS;
}

/**
 * @brief Release a previously acquired block back to the pool.
 *
 * @details O(1). block is validated in full -- non-NULL, within this
 *          pool's storage region (guarded against uintptr_t overflow when
 *          computing the region's end address), block-aligned, and
 *          currently allocated -- before any state is mutated; any failed
 *          check returns MEMORY_POOL_ERROR_INVALID_BLOCK immediately with
 *          nothing touched. Address comparison/subtraction is done via
 *          uintptr_t, not raw pointer arithmetic against storage, since
 *          block may be a foreign pointer and C only guarantees pointer
 *          comparison/subtraction within the same array -- see design doc
 *          Section 6 for why this matters. Unlike memoryPoolAcquire(), a
 *          NULL block here is folded into MEMORY_POOL_ERROR_INVALID_BLOCK
 *          rather than MEMORY_POOL_ERROR_NULL_POINTER, since it is simply
 *          the first of several ways a block pointer can fail validation,
 *          not a distinct category of error -- matching what the header
 *          documents.
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
MemoryPoolStatus_t memoryPoolRelease(MemoryPool_t *pool, void *block)
{
    MemoryPoolStatus_t status;
    uintptr_t storageStart;
    uintptr_t storageEnd;
    uintptr_t pointer;
    uintptr_t totalSize;
    size_t index;

    status = validatePool(pool);
    if (status != MEMORY_POOL_SUCCESS)
    {
        return status;
    }

    if (block == NULL)
    {
        return MEMORY_POOL_ERROR_INVALID_BLOCK;
    }

    storageStart = (uintptr_t)pool->storage;
    totalSize = (uintptr_t)(pool->blockSize * pool->blockCount);

    /* Guards storageStart + totalSize against wrapping uintptr_t. For a
     * pool whose storage argument at Init genuinely was blockSize *
     * blockCount bytes, this can never trigger -- a real object's
     * one-past-the-end address is guaranteed representable. It can only
     * trigger if the caller violated that Init-time contract (passed
     * dimensions larger than the real buffer). Note this makes the
     * outcome a static property of this pool's configuration, not of
     * which block is being released: once triggered, every future
     * Release() on this same pool hits it and returns
     * MEMORY_POOL_ERROR_INVALID_BLOCK unconditionally, valid blocks
     * included, since the pool's own notion of its storage bounds is
     * already unrepresentable. This is intentional per explicit review
     * decision, not something an implementation bug could avoid -- the
     * alternative (catching this once in Init instead) was considered and
     * not taken. */
    if (storageStart > (UINTPTR_MAX - totalSize))
    {
        return MEMORY_POOL_ERROR_INVALID_BLOCK;
    }

    storageEnd = storageStart + totalSize;
    pointer = (uintptr_t)block;

    if ((pointer < storageStart) || (pointer >= storageEnd))
    {
        return MEMORY_POOL_ERROR_INVALID_BLOCK;
    }

    if (((pointer - storageStart) % pool->blockSize) != 0U)
    {
        return MEMORY_POOL_ERROR_INVALID_BLOCK;
    }

    index = (pointer - storageStart) / pool->blockSize;

    if (index >= pool->blockCount)
    {
        return MEMORY_POOL_ERROR_INVALID_BLOCK;
    }

    if (pool->state[index] != (uint8_t)MEMORY_POOL_BLOCK_ALLOCATED)
    {
        return MEMORY_POOL_ERROR_INVALID_BLOCK;
    }

    pool->state[index] = (uint8_t)MEMORY_POOL_BLOCK_FREE;
    pool->freeStack[pool->freeCount] = index;
    pool->freeCount++;

    return MEMORY_POOL_SUCCESS;
}

/**
 * @brief Get the number of blocks currently free.
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
MemoryPoolStatus_t memoryPoolGetFreeCount(const MemoryPool_t *pool, size_t *freeCount)
{
    MemoryPoolStatus_t status;

    if (freeCount == NULL)
    {
        return MEMORY_POOL_ERROR_NULL_POINTER;
    }

    status = validatePool(pool);
    if (status != MEMORY_POOL_SUCCESS)
    {
        return status;
    }

    *freeCount = pool->freeCount;

    return MEMORY_POOL_SUCCESS;
}

/**
 * @brief Get the number of blocks currently acquired.
 *
 * @details Computed as blockCount - freeCount; no separate counter is
 *          maintained for this.
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
MemoryPoolStatus_t memoryPoolGetUsedCount(const MemoryPool_t *pool, size_t *usedCount)
{
    MemoryPoolStatus_t status;

    if (usedCount == NULL)
    {
        return MEMORY_POOL_ERROR_NULL_POINTER;
    }

    status = validatePool(pool);
    if (status != MEMORY_POOL_SUCCESS)
    {
        return status;
    }

    *usedCount = pool->blockCount - pool->freeCount;

    return MEMORY_POOL_SUCCESS;
}

/**
 * @brief Get the total block capacity of the pool.
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
MemoryPoolStatus_t memoryPoolGetCapacity(const MemoryPool_t *pool, size_t *capacity)
{
    MemoryPoolStatus_t status;

    if (capacity == NULL)
    {
        return MEMORY_POOL_ERROR_NULL_POINTER;
    }

    status = validatePool(pool);
    if (status != MEMORY_POOL_SUCCESS)
    {
        return status;
    }

    *capacity = pool->blockCount;

    return MEMORY_POOL_SUCCESS;
}

/**
 * @brief Get the size of one block, in bytes.
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
MemoryPoolStatus_t memoryPoolGetBlockSize(const MemoryPool_t *pool, size_t *blockSize)
{
    MemoryPoolStatus_t status;

    if (blockSize == NULL)
    {
        return MEMORY_POOL_ERROR_NULL_POINTER;
    }

    status = validatePool(pool);
    if (status != MEMORY_POOL_SUCCESS)
    {
        return status;
    }

    *blockSize = pool->blockSize;

    return MEMORY_POOL_SUCCESS;
}

/*****************************************End of memoryPool.c*****************************************/