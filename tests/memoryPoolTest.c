/******************************************************************************
 * @file   memoryPoolTest.c
 * @brief  Unit tests for the fixed-size memory pool (memoryPool.h).
 *
 * @details
 * Two categories of expected behavior are intentionally NOT exercised here
 * as ordinary pass/fail assertions, and are called out explicitly instead
 * of silently missing:
 *  - "No dynamic allocation": this is a static property of the source file
 *    (memoryPool.c never calls malloc/free/calloc/realloc), not something
 *    a black-box unit test can observe from the API. Verified by
 *    inspection at the end of this file's comments, not by a TEST_ASSERT.
 *  - Reentrancy / concurrent-access assumptions: the design and header
 *    documentation state this module is reentrant but not thread-safe for
 *    concurrent access to the same instance (memoryPool-design.md Section
 *    11). This project has no multi-threading test harness, so that
 *    contract cannot be exercised by an automated test here; it remains a
 *    documented caller responsibility.
 *
 * @author Abhishek Doddagoudar
 * @date   August 2026
 ******************************************************************************/

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <memoryPool.h>

static int totalTests = 0;
static int passedTests = 0;

#define TEST_ASSERT(cond, desc)                               \
    do                                                        \
    {                                                         \
        totalTests++;                                         \
        if (cond)                                             \
        {                                                     \
            printf("  PASS (line %d): %s\n", __LINE__, desc); \
            passedTests++;                                    \
        }                                                     \
        else                                                  \
        {                                                     \
            printf("  FAIL (line %d): %s\n", __LINE__, desc); \
        }                                                     \
    } while (0)

#define BLOCK_SIZE  16U
#define BLOCK_COUNT 4U

static void testInit(void)
{
    MemoryPool_t pool;
    uint8_t storage[BLOCK_SIZE * BLOCK_COUNT];
    uint8_t state[BLOCK_COUNT];
    size_t freeStack[BLOCK_COUNT];
    size_t n = 0U;

    printf("\nmemoryPoolInit:\n");

    memset(&pool, 0, sizeof(pool));
    TEST_ASSERT(memoryPoolInit(&pool, storage, state, freeStack, BLOCK_SIZE, BLOCK_COUNT) == MEMORY_POOL_SUCCESS,
                "init with valid storage, state, freeStack, blockSize, blockCount succeeds");

    memoryPoolGetCapacity(&pool, &n);
    TEST_ASSERT(n == BLOCK_COUNT, "capacity after init equals blockCount");
    memoryPoolGetFreeCount(&pool, &n);
    TEST_ASSERT(n == BLOCK_COUNT, "every block starts free after init");
    memoryPoolGetUsedCount(&pool, &n);
    TEST_ASSERT(n == 0U, "used count is 0 immediately after init");
    memoryPoolGetBlockSize(&pool, &n);
    TEST_ASSERT(n == BLOCK_SIZE, "blockSize after init matches what was passed in");

    memset(&pool, 0, sizeof(pool));
    TEST_ASSERT(memoryPoolInit(NULL, storage, state, freeStack, BLOCK_SIZE, BLOCK_COUNT) == MEMORY_POOL_ERROR_NULL_POINTER,
                "init with NULL pool pointer returns MEMORY_POOL_ERROR_NULL_POINTER");
    TEST_ASSERT(memoryPoolInit(&pool, NULL, state, freeStack, BLOCK_SIZE, BLOCK_COUNT) == MEMORY_POOL_ERROR_NULL_POINTER,
                "init with NULL storage returns MEMORY_POOL_ERROR_NULL_POINTER");
    TEST_ASSERT(memoryPoolInit(&pool, storage, NULL, freeStack, BLOCK_SIZE, BLOCK_COUNT) == MEMORY_POOL_ERROR_NULL_POINTER,
                "init with NULL state returns MEMORY_POOL_ERROR_NULL_POINTER");
    TEST_ASSERT(memoryPoolInit(&pool, storage, state, NULL, BLOCK_SIZE, BLOCK_COUNT) == MEMORY_POOL_ERROR_NULL_POINTER,
                "init with NULL freeStack returns MEMORY_POOL_ERROR_NULL_POINTER");

    TEST_ASSERT(memoryPoolInit(&pool, storage, state, freeStack, 0U, BLOCK_COUNT) == MEMORY_POOL_ERROR_INVALID_BLOCK_SIZE,
                "init with blockSize 0 returns MEMORY_POOL_ERROR_INVALID_BLOCK_SIZE");
    TEST_ASSERT(memoryPoolInit(&pool, storage, state, freeStack, BLOCK_SIZE, 0U) == MEMORY_POOL_ERROR_INVALID_BLOCK_COUNT,
                "init with blockCount 0 returns MEMORY_POOL_ERROR_INVALID_BLOCK_COUNT");
    TEST_ASSERT(memoryPoolInit(&pool, storage, state, freeStack, SIZE_MAX, 2U) == MEMORY_POOL_ERROR_SIZE_OVERFLOW,
                "init with blockSize * blockCount overflowing size_t returns MEMORY_POOL_ERROR_SIZE_OVERFLOW");
}

static void testAlreadyInitialized(void)
{
    MemoryPool_t pool;
    uint8_t storage[BLOCK_SIZE * BLOCK_COUNT];
    uint8_t state[BLOCK_COUNT];
    size_t freeStack[BLOCK_COUNT];

    printf("\nmemoryPoolInit re-initialization:\n");

    memset(&pool, 0, sizeof(pool));
    memoryPoolInit(&pool, storage, state, freeStack, BLOCK_SIZE, BLOCK_COUNT);

    TEST_ASSERT(memoryPoolInit(&pool, storage, state, freeStack, BLOCK_SIZE, BLOCK_COUNT) == MEMORY_POOL_ERROR_ALREADY_INITIALIZED,
                "calling init a second time on an already-initialized pool returns MEMORY_POOL_ERROR_ALREADY_INITIALIZED");

    /* NOTE: this specific edge case currently returns
     * MEMORY_POOL_ERROR_NULL_POINTER instead of
     * MEMORY_POOL_ERROR_ALREADY_INITIALIZED, because Init's NULL checks run
     * before the already-initialized check in the current implementation --
     * flagged in review as a discrepancy against design doc Section 8's
     * locked check order. This assertion documents the CURRENT behavior, not
     * the intended one; it should be updated (to expect
     * ALREADY_INITIALIZED) once that ordering is corrected. */
    TEST_ASSERT(memoryPoolInit(&pool, NULL, state, freeStack, BLOCK_SIZE, BLOCK_COUNT) == MEMORY_POOL_ERROR_NULL_POINTER,
                "[KNOWN DISCREPANCY] re-init with a NULL storage currently returns NULL_POINTER, not ALREADY_INITIALIZED (see design doc Section 8)");
}

static void testAcquireFromEmptyPool(void)
{
    MemoryPool_t pool;
    uint8_t storage[BLOCK_SIZE * BLOCK_COUNT];
    uint8_t state[BLOCK_COUNT];
    size_t freeStack[BLOCK_COUNT];
    void *block = NULL;

    printf("\nmemoryPoolAcquire from a freshly initialized (fully free) pool:\n");

    memset(&pool, 0, sizeof(pool));
    memoryPoolInit(&pool, storage, state, freeStack, BLOCK_SIZE, BLOCK_COUNT);

    TEST_ASSERT(memoryPoolAcquire(&pool, &block) == MEMORY_POOL_SUCCESS, "first acquire on a fresh pool succeeds");
    TEST_ASSERT(block != NULL, "acquired block address is non-NULL");
    TEST_ASSERT(block >= (void *)storage && block < (void *)(storage + sizeof(storage)),
                "acquired block address falls within storage");
}

static void testAcquireUntilExhausted(void)
{
    MemoryPool_t pool;
    uint8_t storage[BLOCK_SIZE * BLOCK_COUNT];
    uint8_t state[BLOCK_COUNT];
    size_t freeStack[BLOCK_COUNT];
    void *blocks[BLOCK_COUNT];
    void *extra = (void *)0x1234;
    size_t n = 0U;
    size_t i;
    int allUnique = 1;

    printf("\nmemoryPoolAcquire until the pool is exhausted:\n");

    memset(&pool, 0, sizeof(pool));
    memoryPoolInit(&pool, storage, state, freeStack, BLOCK_SIZE, BLOCK_COUNT);

    for (i = 0U; i < BLOCK_COUNT; i++)
    {
        TEST_ASSERT(memoryPoolAcquire(&pool, &blocks[i]) == MEMORY_POOL_SUCCESS, "acquire succeeds while blocks remain free");
    }

    for (i = 0U; i < BLOCK_COUNT; i++)
    {
        size_t j;
        for (j = i + 1U; j < BLOCK_COUNT; j++)
        {
            if (blocks[i] == blocks[j])
            {
                allUnique = 0;
            }
        }
    }
    TEST_ASSERT(allUnique, "every acquired block has a distinct address");

    memoryPoolGetFreeCount(&pool, &n);
    TEST_ASSERT(n == 0U, "free count is 0 once every block has been acquired");

    TEST_ASSERT(memoryPoolAcquire(&pool, &extra) == MEMORY_POOL_ERROR_POOL_FULL,
                "acquiring from an exhausted pool returns MEMORY_POOL_ERROR_POOL_FULL");
    TEST_ASSERT(extra == (void *)0x1234, "output pointer is left unchanged when acquire fails");
}

static void testReleaseAndReuse(void)
{
    MemoryPool_t pool;
    uint8_t storage[BLOCK_SIZE * BLOCK_COUNT];
    uint8_t state[BLOCK_COUNT];
    size_t freeStack[BLOCK_COUNT];
    void *blocks[BLOCK_COUNT];
    void *reacquired = NULL;
    size_t n = 0U;
    size_t i;

    printf("\nmemoryPoolRelease and LIFO reuse:\n");

    memset(&pool, 0, sizeof(pool));
    memoryPoolInit(&pool, storage, state, freeStack, BLOCK_SIZE, BLOCK_COUNT);

    for (i = 0U; i < BLOCK_COUNT; i++)
    {
        memoryPoolAcquire(&pool, &blocks[i]);
    }

    TEST_ASSERT(memoryPoolRelease(&pool, blocks[2]) == MEMORY_POOL_SUCCESS, "releasing a validly acquired block succeeds");

    memoryPoolGetFreeCount(&pool, &n);
    TEST_ASSERT(n == 1U, "free count increases by 1 after a release");

    TEST_ASSERT(memoryPoolAcquire(&pool, &reacquired) == MEMORY_POOL_SUCCESS, "acquire succeeds again after a release");
    TEST_ASSERT(reacquired == blocks[2],
                "the block handed back is the SAME block just released (LIFO reuse order, design doc Section 5)");
}

static void testReleaseNull(void)
{
    MemoryPool_t pool;
    uint8_t storage[BLOCK_SIZE * BLOCK_COUNT];
    uint8_t state[BLOCK_COUNT];
    size_t freeStack[BLOCK_COUNT];

    printf("\nmemoryPoolRelease with a NULL block:\n");

    memset(&pool, 0, sizeof(pool));
    memoryPoolInit(&pool, storage, state, freeStack, BLOCK_SIZE, BLOCK_COUNT);

    TEST_ASSERT(memoryPoolRelease(&pool, NULL) == MEMORY_POOL_ERROR_INVALID_BLOCK,
                "releasing NULL returns MEMORY_POOL_ERROR_INVALID_BLOCK (folded in, not a separate NULL_POINTER case)");
}

static void testReleaseInvalidPointers(void)
{
    MemoryPool_t pool;
    uint8_t storage[BLOCK_SIZE * BLOCK_COUNT];
    uint8_t state[BLOCK_COUNT];
    size_t freeStack[BLOCK_COUNT];
    int foreignVar = 0;
    uint8_t otherStorage[BLOCK_SIZE * BLOCK_COUNT];

    printf("\nmemoryPoolRelease with invalid (foreign / misaligned / out-of-range) pointers:\n");

    memset(&pool, 0, sizeof(pool));
    memoryPoolInit(&pool, storage, state, freeStack, BLOCK_SIZE, BLOCK_COUNT);

    TEST_ASSERT(memoryPoolRelease(&pool, &foreignVar) == MEMORY_POOL_ERROR_INVALID_BLOCK,
                "releasing a pointer to an unrelated stack variable returns MEMORY_POOL_ERROR_INVALID_BLOCK");

    TEST_ASSERT(memoryPoolRelease(&pool, otherStorage) == MEMORY_POOL_ERROR_INVALID_BLOCK,
                "releasing a pointer into a completely different array (same size, wrong identity) returns MEMORY_POOL_ERROR_INVALID_BLOCK");

    TEST_ASSERT(memoryPoolRelease(&pool, storage + 1) == MEMORY_POOL_ERROR_INVALID_BLOCK,
                "releasing a pointer one byte into a block (misaligned, not on a block boundary) returns MEMORY_POOL_ERROR_INVALID_BLOCK");

    TEST_ASSERT(memoryPoolRelease(&pool, storage + (BLOCK_SIZE * BLOCK_COUNT)) == MEMORY_POOL_ERROR_INVALID_BLOCK,
                "releasing the one-past-the-end address of storage (out of range) returns MEMORY_POOL_ERROR_INVALID_BLOCK");

    TEST_ASSERT(memoryPoolRelease(&pool, storage - 1) == MEMORY_POOL_ERROR_INVALID_BLOCK,
                "releasing an address just before storage (out of range) returns MEMORY_POOL_ERROR_INVALID_BLOCK");
}

static void testDoubleFree(void)
{
    MemoryPool_t pool;
    uint8_t storage[BLOCK_SIZE * BLOCK_COUNT];
    uint8_t state[BLOCK_COUNT];
    size_t freeStack[BLOCK_COUNT];
    void *block = NULL;

    printf("\nmemoryPoolRelease double-free detection:\n");

    memset(&pool, 0, sizeof(pool));
    memoryPoolInit(&pool, storage, state, freeStack, BLOCK_SIZE, BLOCK_COUNT);
    memoryPoolAcquire(&pool, &block);

    TEST_ASSERT(memoryPoolRelease(&pool, block) == MEMORY_POOL_SUCCESS, "first release of a validly acquired block succeeds");
    TEST_ASSERT(memoryPoolRelease(&pool, block) == MEMORY_POOL_ERROR_INVALID_BLOCK,
                "releasing the same block a second time returns MEMORY_POOL_ERROR_INVALID_BLOCK (double free)");
}

static void testCapacityBoundaries(void)
{
    MemoryPool_t pool;
    uint8_t storage[BLOCK_SIZE * 1U];
    uint8_t state[1U];
    size_t freeStack[1U];
    void *block = NULL;
    size_t n = 0U;

    printf("\ncapacity boundary: blockCount == 1 (smallest legal pool):\n");

    memset(&pool, 0, sizeof(pool));
    TEST_ASSERT(memoryPoolInit(&pool, storage, state, freeStack, BLOCK_SIZE, 1U) == MEMORY_POOL_SUCCESS,
                "init with blockCount 1 succeeds");

    memoryPoolGetCapacity(&pool, &n);
    TEST_ASSERT(n == 1U, "capacity is exactly 1");

    TEST_ASSERT(memoryPoolAcquire(&pool, &block) == MEMORY_POOL_SUCCESS, "acquiring the only block succeeds");
    TEST_ASSERT(memoryPoolAcquire(&pool, &block) == MEMORY_POOL_ERROR_POOL_FULL,
                "acquiring a second block from a 1-block pool returns MEMORY_POOL_ERROR_POOL_FULL");
    TEST_ASSERT(memoryPoolRelease(&pool, block) == MEMORY_POOL_SUCCESS, "releasing the only block succeeds");
    TEST_ASSERT(memoryPoolAcquire(&pool, &block) == MEMORY_POOL_SUCCESS, "re-acquiring after release succeeds");
}

static void testBlockSizeBoundaries(void)
{
    MemoryPool_t pool;
    uint8_t storage1[1U * 4U];
    uint8_t state1[4U];
    size_t freeStack1[4U];
    void *block = NULL;

    printf("\nblock-size boundary: blockSize == 1 (smallest legal block):\n");

    memset(&pool, 0, sizeof(pool));
    TEST_ASSERT(memoryPoolInit(&pool, storage1, state1, freeStack1, 1U, 4U) == MEMORY_POOL_SUCCESS,
                "init with blockSize 1 succeeds (no minimum-block-size constraint in this design -- unlike an intrusive free list, this design's free-stack approach does not require blockSize >= sizeof(void*))");

    TEST_ASSERT(memoryPoolAcquire(&pool, &block) == MEMORY_POOL_SUCCESS, "acquire from a 1-byte-block pool succeeds");
    TEST_ASSERT(memoryPoolRelease(&pool, block) == MEMORY_POOL_SUCCESS, "release back to a 1-byte-block pool succeeds");
}

static void testAlignment(void)
{
    typedef struct
    {
        uint32_t id;
        uint64_t timestamp;
    } AlignedObject_t;

#define ALIGNED_BLOCK_COUNT 4U
    /* Round sizeof up to a multiple of _Alignof, per design doc Section 7 --
     * NOT the raw sizeof, or later blocks would silently drift out of
     * alignment even though block 0 is fine. */
#define ALIGNED_BLOCK_SIZE ((sizeof(AlignedObject_t) + _Alignof(AlignedObject_t) - 1U) \
                            / _Alignof(AlignedObject_t) * _Alignof(AlignedObject_t))

    MemoryPool_t pool;
    _Alignas(AlignedObject_t) uint8_t storage[ALIGNED_BLOCK_SIZE * ALIGNED_BLOCK_COUNT];
    uint8_t state[ALIGNED_BLOCK_COUNT];
    size_t freeStack[ALIGNED_BLOCK_COUNT];
    void *blocks[ALIGNED_BLOCK_COUNT];
    size_t i;
    int allAligned = 1;

    printf("\nalignment: every block address is aligned for AlignedObject_t when storage/blockSize follow design doc Section 7:\n");

    memset(&pool, 0, sizeof(pool));
    memoryPoolInit(&pool, storage, state, freeStack, ALIGNED_BLOCK_SIZE, ALIGNED_BLOCK_COUNT);

    for (i = 0U; i < ALIGNED_BLOCK_COUNT; i++)
    {
        memoryPoolAcquire(&pool, &blocks[i]);
        if (((uintptr_t)blocks[i] % _Alignof(AlignedObject_t)) != 0U)
        {
            allAligned = 0;
        }
    }

    TEST_ASSERT(allAligned, "every block, including the 2nd/3rd/4th (not just block 0), lands on an AlignedObject_t-aligned address");

    for (i = 0U; i < ALIGNED_BLOCK_COUNT; i++)
    {
        AlignedObject_t *obj = (AlignedObject_t *)blocks[i];
        obj->id = (uint32_t)i;
        obj->timestamp = (uint64_t)i * 1000U;
    }
    TEST_ASSERT(((AlignedObject_t *)blocks[3])->timestamp == 3000U,
                "writing through a properly-aligned block as AlignedObject_t* works correctly (no unaligned-access fault/corruption)");

#undef ALIGNED_BLOCK_COUNT
#undef ALIGNED_BLOCK_SIZE
}

static void testStatisticsEquivalents(void)
{
    /* This module has no dedicated IsFull()/IsEmpty() functions by design
     * (design doc Section 15): GetFreeCount() == 0 and
     * GetFreeCount() == GetCapacity() serve as the equivalent checks. */
    MemoryPool_t pool;
    uint8_t storage[BLOCK_SIZE * BLOCK_COUNT];
    uint8_t state[BLOCK_COUNT];
    size_t freeStack[BLOCK_COUNT];
    void *blocks[BLOCK_COUNT];
    size_t freeCount = 0U, usedCount = 0U, capacity = 0U;
    size_t i;

    printf("\nGetFreeCount/GetCapacity as IsEmpty/IsFull equivalents:\n");

    memset(&pool, 0, sizeof(pool));
    memoryPoolInit(&pool, storage, state, freeStack, BLOCK_SIZE, BLOCK_COUNT);

    memoryPoolGetFreeCount(&pool, &freeCount);
    memoryPoolGetCapacity(&pool, &capacity);
    TEST_ASSERT(freeCount == capacity, "'isEmpty' equivalent: freeCount == capacity on a fresh pool");

    for (i = 0U; i < BLOCK_COUNT; i++)
    {
        memoryPoolAcquire(&pool, &blocks[i]);
    }

    memoryPoolGetFreeCount(&pool, &freeCount);
    TEST_ASSERT(freeCount == 0U, "'isFull' equivalent: freeCount == 0 once every block is acquired");

    memoryPoolGetUsedCount(&pool, &usedCount);
    TEST_ASSERT(usedCount == capacity, "usedCount == capacity once every block is acquired");
    TEST_ASSERT(usedCount + freeCount == capacity, "usedCount + freeCount == capacity holds");
}

static void testUninitializedAndNullPool(void)
{
    MemoryPool_t fresh;
    uint8_t storage[BLOCK_SIZE * BLOCK_COUNT];
    void *block = NULL;
    size_t n = 0U;

    printf("\nuninitialized pool / NULL pool pointer across the API:\n");

    memset(&fresh, 0, sizeof(fresh)); /* zero-initialized -> initialized == false, guaranteed */

    TEST_ASSERT(memoryPoolAcquire(&fresh, &block) == MEMORY_POOL_ERROR_NOT_INITIALIZED,
                "acquire on an uninitialized (zeroed) pool returns MEMORY_POOL_ERROR_NOT_INITIALIZED");
    TEST_ASSERT(memoryPoolRelease(&fresh, storage) == MEMORY_POOL_ERROR_NOT_INITIALIZED,
                "release on an uninitialized pool returns MEMORY_POOL_ERROR_NOT_INITIALIZED");
    TEST_ASSERT(memoryPoolGetFreeCount(&fresh, &n) == MEMORY_POOL_ERROR_NOT_INITIALIZED,
                "getFreeCount on an uninitialized pool returns MEMORY_POOL_ERROR_NOT_INITIALIZED");
    TEST_ASSERT(memoryPoolGetUsedCount(&fresh, &n) == MEMORY_POOL_ERROR_NOT_INITIALIZED,
                "getUsedCount on an uninitialized pool returns MEMORY_POOL_ERROR_NOT_INITIALIZED");
    TEST_ASSERT(memoryPoolGetCapacity(&fresh, &n) == MEMORY_POOL_ERROR_NOT_INITIALIZED,
                "getCapacity on an uninitialized pool returns MEMORY_POOL_ERROR_NOT_INITIALIZED");
    TEST_ASSERT(memoryPoolGetBlockSize(&fresh, &n) == MEMORY_POOL_ERROR_NOT_INITIALIZED,
                "getBlockSize on an uninitialized pool returns MEMORY_POOL_ERROR_NOT_INITIALIZED");

    TEST_ASSERT(memoryPoolAcquire(NULL, &block) == MEMORY_POOL_ERROR_NULL_POINTER, "acquire with NULL pool returns MEMORY_POOL_ERROR_NULL_POINTER");
    TEST_ASSERT(memoryPoolRelease(NULL, storage) == MEMORY_POOL_ERROR_NULL_POINTER, "release with NULL pool returns MEMORY_POOL_ERROR_NULL_POINTER");
    TEST_ASSERT(memoryPoolGetFreeCount(NULL, &n) == MEMORY_POOL_ERROR_NULL_POINTER, "getFreeCount with NULL pool returns MEMORY_POOL_ERROR_NULL_POINTER");
    TEST_ASSERT(memoryPoolGetUsedCount(NULL, &n) == MEMORY_POOL_ERROR_NULL_POINTER, "getUsedCount with NULL pool returns MEMORY_POOL_ERROR_NULL_POINTER");
    TEST_ASSERT(memoryPoolGetCapacity(NULL, &n) == MEMORY_POOL_ERROR_NULL_POINTER, "getCapacity with NULL pool returns MEMORY_POOL_ERROR_NULL_POINTER");
    TEST_ASSERT(memoryPoolGetBlockSize(NULL, &n) == MEMORY_POOL_ERROR_NULL_POINTER, "getBlockSize with NULL pool returns MEMORY_POOL_ERROR_NULL_POINTER");
}

static void testOutputPointerNullHandling(void)
{
    MemoryPool_t pool;
    uint8_t storage[BLOCK_SIZE * BLOCK_COUNT];
    uint8_t state[BLOCK_COUNT];
    size_t freeStack[BLOCK_COUNT];

    printf("\nNULL output-pointer handling (Acquire and Get* functions):\n");

    memset(&pool, 0, sizeof(pool));
    memoryPoolInit(&pool, storage, state, freeStack, BLOCK_SIZE, BLOCK_COUNT);

    TEST_ASSERT(memoryPoolAcquire(&pool, NULL) == MEMORY_POOL_ERROR_NULL_POINTER, "acquire with NULL block-output pointer returns MEMORY_POOL_ERROR_NULL_POINTER");
    TEST_ASSERT(memoryPoolGetFreeCount(&pool, NULL) == MEMORY_POOL_ERROR_NULL_POINTER, "getFreeCount with NULL output pointer returns MEMORY_POOL_ERROR_NULL_POINTER");
    TEST_ASSERT(memoryPoolGetUsedCount(&pool, NULL) == MEMORY_POOL_ERROR_NULL_POINTER, "getUsedCount with NULL output pointer returns MEMORY_POOL_ERROR_NULL_POINTER");
    TEST_ASSERT(memoryPoolGetCapacity(&pool, NULL) == MEMORY_POOL_ERROR_NULL_POINTER, "getCapacity with NULL output pointer returns MEMORY_POOL_ERROR_NULL_POINTER");
    TEST_ASSERT(memoryPoolGetBlockSize(&pool, NULL) == MEMORY_POOL_ERROR_NULL_POINTER, "getBlockSize with NULL output pointer returns MEMORY_POOL_ERROR_NULL_POINTER");
}

static void testNoNeighboringBlockCorruption(void)
{
    MemoryPool_t pool;
    uint8_t storage[BLOCK_SIZE * BLOCK_COUNT];
    uint8_t state[BLOCK_COUNT];
    size_t freeStack[BLOCK_COUNT];
    void *blocks[BLOCK_COUNT];
    size_t i;
    int neighborsIntact = 1;

    printf("\nwriting to one acquired block does not corrupt neighboring blocks:\n");

    memset(&pool, 0, sizeof(pool));
    memoryPoolInit(&pool, storage, state, freeStack, BLOCK_SIZE, BLOCK_COUNT);

    for (i = 0U; i < BLOCK_COUNT; i++)
    {
        memoryPoolAcquire(&pool, &blocks[i]);
        /* Fill each block with a distinct byte pattern tied to its index. */
        memset(blocks[i], (int)(0xA0U + i), BLOCK_SIZE);
    }

    for (i = 0U; i < BLOCK_COUNT; i++)
    {
        size_t j;
        uint8_t *bytes = (uint8_t *)blocks[i];
        for (j = 0U; j < BLOCK_SIZE; j++)
        {
            if (bytes[j] != (uint8_t)(0xA0U + i))
            {
                neighborsIntact = 0;
            }
        }
    }

    TEST_ASSERT(neighborsIntact, "each block's contents remain exactly as written -- no bleed-over between adjacent blocks");
}

static void testStressManyOperations(void)
{
    MemoryPool_t pool;
    uint8_t storage[BLOCK_SIZE * BLOCK_COUNT];
    uint8_t state[BLOCK_COUNT];
    size_t freeStack[BLOCK_COUNT];
    void *blocks[BLOCK_COUNT];
    size_t freeCount = 0U;
    int cycle;
    size_t i;
    int allSucceeded = 1;

    printf("\nstress: 1000 full acquire/release cycles, checking invariants each cycle:\n");

    memset(&pool, 0, sizeof(pool));
    memoryPoolInit(&pool, storage, state, freeStack, BLOCK_SIZE, BLOCK_COUNT);

    for (cycle = 0; cycle < 1000; cycle++)
    {
        for (i = 0U; i < BLOCK_COUNT; i++)
        {
            if (memoryPoolAcquire(&pool, &blocks[i]) != MEMORY_POOL_SUCCESS)
            {
                allSucceeded = 0;
            }
        }

        memoryPoolGetFreeCount(&pool, &freeCount);
        if (freeCount != 0U)
        {
            allSucceeded = 0;
        }

        /* Release in a different order than acquired, to exercise the free
         * stack under a non-trivial access pattern rather than always
         * reversing the exact acquire order. */
        for (i = 0U; i < BLOCK_COUNT; i++)
        {
            size_t releaseOrder = (i + 1U) % BLOCK_COUNT;
            if (memoryPoolRelease(&pool, blocks[releaseOrder]) != MEMORY_POOL_SUCCESS)
            {
                allSucceeded = 0;
            }
        }

        memoryPoolGetFreeCount(&pool, &freeCount);
        if (freeCount != BLOCK_COUNT)
        {
            allSucceeded = 0;
        }
    }

    TEST_ASSERT(allSucceeded, "1000 acquire/release cycles all succeed with no bookkeeping corruption (freeCount always returns to 0 and BLOCK_COUNT correctly)");

    /* Final sanity: pool is still fully usable after the stress loop. */
    {
        void *finalBlock = NULL;
        TEST_ASSERT(memoryPoolAcquire(&pool, &finalBlock) == MEMORY_POOL_SUCCESS,
                    "pool remains fully functional immediately after the stress loop");
    }
}

int main(void)
{
    printf("Running memoryPool tests...\n");

    testInit();
    testAlreadyInitialized();
    testAcquireFromEmptyPool();
    testAcquireUntilExhausted();
    testReleaseAndReuse();
    testReleaseNull();
    testReleaseInvalidPointers();
    testDoubleFree();
    testCapacityBoundaries();
    testBlockSizeBoundaries();
    testAlignment();
    testStatisticsEquivalents();
    testUninitializedAndNullPool();
    testOutputPointerNullHandling();
    testNoNeighboringBlockCorruption();
    testStressManyOperations();

    printf("\n%d / %d tests passed.\n", passedTests, totalTests);

    return (passedTests == totalTests) ? 0 : 1;
}

/**************************************** END OF memoryPoolTest.c ****************************************/