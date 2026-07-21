/******************************************************************************
 * @file   testRingBuffer.c
 * @brief  Unit tests for ring buffer utilities (ringBuffer.h).
 *
 * @author Abhishek Doddagoudar
 *
 * @date   July 2026
 ******************************************************************************/

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ringBuffer.h>

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

static void testInit(void)
{
    printf("ringBufferInit:\n");

    RingBuffer_t rb;
    uint8_t storage[8];

    TEST_ASSERT(ringBufferInit(&rb, storage, sizeof(storage)) == RING_BUFFER_OK,
                "init with valid buffer and non-zero size succeeds");
    TEST_ASSERT(rb.buffer == storage, "buffer pointer is stored correctly");
    TEST_ASSERT(rb.capacity == sizeof(storage), "capacity is stored correctly");
    TEST_ASSERT(rb.head == 0, "head starts at 0");
    TEST_ASSERT(rb.tail == 0, "tail starts at 0");

    TEST_ASSERT(ringBufferInit(NULL, storage, sizeof(storage)) == RING_BUFFER_INVALID_PARAMETER,
                "init with NULL ringBuffer pointer returns RING_BUFFER_INVALID_PARAMETER");
    TEST_ASSERT(ringBufferInit(&rb, NULL, sizeof(storage)) == RING_BUFFER_INVALID_PARAMETER,
                "init with NULL buffer pointer returns RING_BUFFER_INVALID_PARAMETER");
    TEST_ASSERT(ringBufferInit(&rb, storage, 0) == RING_BUFFER_INVALID_PARAMETER,
                "init with zero size returns RING_BUFFER_INVALID_PARAMETER");
}

static void testPushPopBasic(void)
{
    printf("ringBufferPush / ringBufferPop basic:\n");

    RingBuffer_t rb;
    uint8_t storage[4];
    ringBufferInit(&rb, storage, sizeof(storage));

    TEST_ASSERT(ringBufferPush(&rb, 0xAAU) == RING_BUFFER_OK, "push a byte into an empty buffer succeeds");

    uint8_t data = 0;
    TEST_ASSERT(ringBufferPop(&rb, &data) == RING_BUFFER_OK, "pop from a non-empty buffer succeeds");
    TEST_ASSERT(data == 0xAAU, "popped byte matches the byte that was pushed");

    TEST_ASSERT(ringBufferPop(&rb, &data) == RING_BUFFER_EMPTY,
                "popping from a now-empty buffer returns RING_BUFFER_EMPTY");
}

static void testPushPopOrderingFIFO(void)
{
    printf("ringBufferPush / ringBufferPop FIFO ordering:\n");

    RingBuffer_t rb;
    uint8_t storage[8];
    ringBufferInit(&rb, storage, sizeof(storage));

    const uint8_t values[] = {1, 2, 3, 4, 5};
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++)
    {
        TEST_ASSERT(ringBufferPush(&rb, values[i]) == RING_BUFFER_OK, "push value into buffer succeeds");
    }

    int inOrder = 1;
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++)
    {
        uint8_t data = 0;
        ringBufferPop(&rb, &data);
        if (data != values[i])
        {
            inOrder = 0;
        }
    }

    TEST_ASSERT(inOrder, "values pop out in the same order they were pushed (FIFO)");
}

static void testFullBuffer(void)
{
    printf("ringBufferPush on a full buffer:\n");

    RingBuffer_t rb;
    uint8_t storage[4]; /* usable capacity is 3, due to the one-empty-slot technique */
    ringBufferInit(&rb, storage, sizeof(storage));

    TEST_ASSERT(ringBufferPush(&rb, 1) == RING_BUFFER_OK, "push 1st byte succeeds");
    TEST_ASSERT(ringBufferPush(&rb, 2) == RING_BUFFER_OK, "push 2nd byte succeeds");
    TEST_ASSERT(ringBufferPush(&rb, 3) == RING_BUFFER_OK, "push 3rd byte fills the usable capacity");

    TEST_ASSERT(ringBufferIsFull(&rb) == true, "buffer reports full once usable capacity is reached");
    TEST_ASSERT(ringBufferPush(&rb, 4) == RING_BUFFER_FULL, "pushing to a full buffer returns RING_BUFFER_FULL");

    uint8_t data = 0;
    ringBufferPop(&rb, &data);
    TEST_ASSERT(ringBufferIsFull(&rb) == false, "buffer is no longer full after a pop frees a slot");
    TEST_ASSERT(ringBufferPush(&rb, 4) == RING_BUFFER_OK, "push succeeds again after freeing a slot");
}

static void testEmptyBuffer(void)
{
    printf("ringBufferIsEmpty / ringBufferPop on an empty buffer:\n");

    RingBuffer_t rb;
    uint8_t storage[4];
    ringBufferInit(&rb, storage, sizeof(storage));

    TEST_ASSERT(ringBufferIsEmpty(&rb) == true, "freshly initialized buffer is empty");

    uint8_t data = 0;
    TEST_ASSERT(ringBufferPop(&rb, &data) == RING_BUFFER_EMPTY, "pop on an empty buffer returns RING_BUFFER_EMPTY");
    TEST_ASSERT(ringBufferPeek(&rb, &data) == RING_BUFFER_EMPTY, "peek on an empty buffer returns RING_BUFFER_EMPTY");

    ringBufferPush(&rb, 0x11U);
    TEST_ASSERT(ringBufferIsEmpty(&rb) == false, "buffer is not empty after a push");
}

static void testPeekDoesNotRemove(void)
{
    printf("ringBufferPeek:\n");

    RingBuffer_t rb;
    uint8_t storage[4];
    ringBufferInit(&rb, storage, sizeof(storage));

    ringBufferPush(&rb, 0x42U);

    uint8_t peeked = 0;
    TEST_ASSERT(ringBufferPeek(&rb, &peeked) == RING_BUFFER_OK, "peek on a non-empty buffer succeeds");
    TEST_ASSERT(peeked == 0x42U, "peeked value matches the front of the buffer");

    size_t size = 0;
    ringBufferGetSize(&rb, &size);
    TEST_ASSERT(size == 1, "size is unchanged after a peek (peek does not remove data)");

    uint8_t popped = 0;
    ringBufferPop(&rb, &popped);
    TEST_ASSERT(popped == peeked, "the value popped afterward matches what was peeked");
}

static void testClear(void)
{
    printf("ringBufferClear:\n");

    RingBuffer_t rb;
    uint8_t storage[4];
    ringBufferInit(&rb, storage, sizeof(storage));

    ringBufferPush(&rb, 1);
    ringBufferPush(&rb, 2);

    TEST_ASSERT(ringBufferClear(&rb) == RING_BUFFER_OK, "clear on a non-empty buffer succeeds");
    TEST_ASSERT(ringBufferIsEmpty(&rb) == true, "buffer is empty after clear");

    size_t size = 0;
    ringBufferGetSize(&rb, &size);
    TEST_ASSERT(size == 0, "size is 0 after clear");

    TEST_ASSERT(ringBufferPush(&rb, 9) == RING_BUFFER_OK, "buffer is usable again after clear");
}

static void testGetSize(void)
{
    printf("ringBufferGetSize:\n");

    RingBuffer_t rb;
    uint8_t storage[8];
    ringBufferInit(&rb, storage, sizeof(storage));

    size_t size = 0;
    TEST_ASSERT(ringBufferGetSize(&rb, &size) == RING_BUFFER_OK, "getSize on a valid buffer succeeds");
    TEST_ASSERT(size == 0, "size of a freshly initialized buffer is 0");

    ringBufferPush(&rb, 1);
    ringBufferPush(&rb, 2);
    ringBufferPush(&rb, 3);
    ringBufferGetSize(&rb, &size);
    TEST_ASSERT(size == 3, "size reflects the number of pushed elements");

    uint8_t data = 0;
    ringBufferPop(&rb, &data);
    ringBufferGetSize(&rb, &size);
    TEST_ASSERT(size == 2, "size decreases after a pop");

    TEST_ASSERT(ringBufferGetSize(NULL, &size) == RING_BUFFER_NULL_POINTER,
                "getSize with NULL ringBuffer pointer returns RING_BUFFER_NULL_POINTER");
    TEST_ASSERT(ringBufferGetSize(&rb, NULL) == RING_BUFFER_NULL_POINTER,
                "getSize with NULL size pointer returns RING_BUFFER_NULL_POINTER");
}

static void testGetCapacity(void)
{
    printf("ringBufferGetCapacity:\n");

    RingBuffer_t rb;
    uint8_t storage[8];
    ringBufferInit(&rb, storage, sizeof(storage));

    size_t capacity = 0;
    TEST_ASSERT(ringBufferGetCapacity(&rb, &capacity) == RING_BUFFER_OK, "getCapacity on a valid buffer succeeds");
    TEST_ASSERT(capacity == sizeof(storage) - 1,
                "usable capacity is one less than the underlying storage size");

    TEST_ASSERT(ringBufferGetCapacity(NULL, &capacity) == RING_BUFFER_NULL_POINTER,
                "getCapacity with NULL ringBuffer pointer returns RING_BUFFER_NULL_POINTER");
    TEST_ASSERT(ringBufferGetCapacity(&rb, NULL) == RING_BUFFER_NULL_POINTER,
                "getCapacity with NULL capacity pointer returns RING_BUFFER_NULL_POINTER");
}

static void testWraparound(void)
{
    printf("wraparound behavior (head/tail wrapping past the end of storage):\n");

    RingBuffer_t rb;
    uint8_t storage[4]; /* usable capacity 3 */
    ringBufferInit(&rb, storage, sizeof(storage));

    /* Fill, drain, and refill repeatedly so head/tail wrap around the
     * underlying storage several times, exercising the modulo wraparound
     * logic in push/pop. */
    int wrappedCorrectly = 1;
    uint8_t nextValue = 0;
    for (int cycle = 0; cycle < 5; cycle++)
    {
        for (int i = 0; i < 3; i++)
        {
            if (ringBufferPush(&rb, nextValue) != RING_BUFFER_OK)
            {
                wrappedCorrectly = 0;
            }
            nextValue++;
        }

        for (int i = 0; i < 3; i++)
        {
            uint8_t data = 0;
            if (ringBufferPop(&rb, &data) != RING_BUFFER_OK)
            {
                wrappedCorrectly = 0;
            }
        }
    }

    TEST_ASSERT(wrappedCorrectly, "repeated fill/drain cycles succeed across storage wraparound");
    TEST_ASSERT(ringBufferIsEmpty(&rb) == true, "buffer is empty after equal pushes and pops across wraparound");
}

static void testNullPointerHandling(void)
{
    printf("negative: NULL pointer handling across the API:\n");

    RingBuffer_t rb;
    uint8_t storage[4];
    ringBufferInit(&rb, storage, sizeof(storage));

    uint8_t data = 0;

    TEST_ASSERT(ringBufferPush(NULL, 1) == RING_BUFFER_NULL_POINTER,
                "push with NULL ringBuffer pointer returns RING_BUFFER_NULL_POINTER");
    TEST_ASSERT(ringBufferPop(NULL, &data) == RING_BUFFER_NULL_POINTER,
                "pop with NULL ringBuffer pointer returns RING_BUFFER_NULL_POINTER");
    TEST_ASSERT(ringBufferPop(&rb, NULL) == RING_BUFFER_NULL_POINTER,
                "pop with NULL data pointer returns RING_BUFFER_NULL_POINTER");
    TEST_ASSERT(ringBufferPeek(NULL, &data) == RING_BUFFER_NULL_POINTER,
                "peek with NULL ringBuffer pointer returns RING_BUFFER_NULL_POINTER");
    TEST_ASSERT(ringBufferPeek(&rb, NULL) == RING_BUFFER_NULL_POINTER,
                "peek with NULL data pointer returns RING_BUFFER_NULL_POINTER");
    TEST_ASSERT(ringBufferClear(NULL) == RING_BUFFER_NULL_POINTER,
                "clear with NULL ringBuffer pointer returns RING_BUFFER_NULL_POINTER");

    TEST_ASSERT(ringBufferIsFull(NULL) == false, "isFull with NULL ringBuffer pointer returns false");
    TEST_ASSERT(ringBufferIsEmpty(NULL) == true, "isEmpty with NULL ringBuffer pointer returns true");
}

int main(void)
{
    printf("Running ringBuffer tests...\n\n");

    testInit();
    testPushPopBasic();
    testPushPopOrderingFIFO();
    testFullBuffer();
    testEmptyBuffer();
    testPeekDoesNotRemove();
    testClear();
    testGetSize();
    testGetCapacity();
    testWraparound();
    testNullPointerHandling();

    printf("\n%d / %d tests passed.\n", passedTests, totalTests);

    return (passedTests == totalTests) ? 0 : 1;
}

/**************************************** END OF testRingBuffer.c ****************************************/