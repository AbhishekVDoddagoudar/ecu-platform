/******************************************************************************
 * @file   testRingBuffer.c
 * @brief  Unit tests for ring buffer utilities (ringBuffer.h).
 *
 * @details
 * ringBuffer.c uses a generic, element-size-based API: ringBufferInit takes
 * (storage, capacity, elementSize), and Push/Pop/Peek all take void*
 * pointers to a caller-supplied element rather than a raw byte value. All
 * status codes are RING_BUFFER_ERROR_* / RING_BUFFER_SUCCESS -- there is no
 * RING_BUFFER_NULL_POINTER, RING_BUFFER_FULL, RING_BUFFER_EMPTY, or
 * RING_BUFFER_INVALID_PARAMETER (those names never existed in this API).
 *
 * @author Abhishek Doddagoudar
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
    RingBuffer_t rb;
    uint8_t storage[8];

    printf("\nringBufferInit:\n");

    TEST_ASSERT(ringBufferInit(&rb, storage, sizeof(storage), 1U) == RING_BUFFER_SUCCESS,
                "init with valid storage, capacity, and elementSize succeeds");
    TEST_ASSERT(rb.buffer == storage, "buffer pointer is stored correctly");
    TEST_ASSERT(rb.capacity == sizeof(storage), "capacity is stored correctly");
    TEST_ASSERT(rb.elementSize == 1U, "elementSize is stored correctly");
    TEST_ASSERT(rb.head == 0U, "head starts at 0");
    TEST_ASSERT(rb.tail == 0U, "tail starts at 0");

    TEST_ASSERT(ringBufferInit(NULL, storage, sizeof(storage), 1U) == RING_BUFFER_ERROR_NULL_POINTER,
                "init with NULL ringBuffer pointer returns RING_BUFFER_ERROR_NULL_POINTER");
    TEST_ASSERT(ringBufferInit(&rb, NULL, sizeof(storage), 1U) == RING_BUFFER_ERROR_NULL_POINTER,
                "init with NULL storage pointer returns RING_BUFFER_ERROR_NULL_POINTER");
    TEST_ASSERT(ringBufferInit(&rb, storage, 0U, 1U) == RING_BUFFER_ERROR_INVALID_CAPACITY,
                "init with capacity 0 returns RING_BUFFER_ERROR_INVALID_CAPACITY");
    TEST_ASSERT(ringBufferInit(&rb, storage, 1U, 1U) == RING_BUFFER_ERROR_INVALID_CAPACITY,
                "init with capacity 1 returns RING_BUFFER_ERROR_INVALID_CAPACITY (need >= 2 to distinguish full/empty)");
    TEST_ASSERT(ringBufferInit(&rb, storage, sizeof(storage), 0U) == RING_BUFFER_ERROR_INVALID_ELEMENT_SIZE,
                "init with elementSize 0 returns RING_BUFFER_ERROR_INVALID_ELEMENT_SIZE");
}

static void testPushPopBasic(void)
{
    RingBuffer_t rb;
    uint8_t storage[4];
    uint8_t valueIn = 0xAAU;
    uint8_t valueOut = 0U;

    printf("\nringBufferPush / ringBufferPop basic:\n");

    ringBufferInit(&rb, storage, sizeof(storage), sizeof(uint8_t));

    TEST_ASSERT(ringBufferPush(&rb, &valueIn) == RING_BUFFER_SUCCESS, "push a byte into an empty buffer succeeds");

    TEST_ASSERT(ringBufferPop(&rb, &valueOut) == RING_BUFFER_SUCCESS, "pop from a non-empty buffer succeeds");
    TEST_ASSERT(valueOut == 0xAAU, "popped byte matches the byte that was pushed");

    TEST_ASSERT(ringBufferPop(&rb, &valueOut) == RING_BUFFER_ERROR_EMPTY,
                "popping from a now-empty buffer returns RING_BUFFER_ERROR_EMPTY");
}

static void testPushPopOrderingFIFO(void)
{
    RingBuffer_t rb;
    uint8_t storage[8];
    const uint8_t values[] = {1U, 2U, 3U, 4U, 5U};
    size_t count = sizeof(values) / sizeof(values[0]);
    int inOrder = 1;
    size_t i;

    printf("\nringBufferPush / ringBufferPop FIFO ordering:\n");

    ringBufferInit(&rb, storage, sizeof(storage), sizeof(uint8_t));

    for (i = 0U; i < count; i++)
    {
        TEST_ASSERT(ringBufferPush(&rb, &values[i]) == RING_BUFFER_SUCCESS, "push value into buffer succeeds");
    }

    for (i = 0U; i < count; i++)
    {
        uint8_t data = 0U;
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
    RingBuffer_t rb;
    uint8_t storage[4]; /* usable capacity is 3, due to the one-empty-slot technique */
    uint8_t one = 1U, two = 2U, three = 3U, four = 4U;
    uint8_t data = 0U;

    printf("\nringBufferPush on a full buffer:\n");

    ringBufferInit(&rb, storage, sizeof(storage), sizeof(uint8_t));

    TEST_ASSERT(ringBufferPush(&rb, &one) == RING_BUFFER_SUCCESS, "push 1st byte succeeds");
    TEST_ASSERT(ringBufferPush(&rb, &two) == RING_BUFFER_SUCCESS, "push 2nd byte succeeds");
    TEST_ASSERT(ringBufferPush(&rb, &three) == RING_BUFFER_SUCCESS, "push 3rd byte fills the usable capacity");

    TEST_ASSERT(ringBufferIsFull(&rb) == true, "buffer reports full once usable capacity is reached");
    TEST_ASSERT(ringBufferPush(&rb, &four) == RING_BUFFER_ERROR_FULL, "pushing to a full buffer returns RING_BUFFER_ERROR_FULL");

    ringBufferPop(&rb, &data);
    TEST_ASSERT(ringBufferIsFull(&rb) == false, "buffer is no longer full after a pop frees a slot");
    TEST_ASSERT(ringBufferPush(&rb, &four) == RING_BUFFER_SUCCESS, "push succeeds again after freeing a slot");
}

static void testEmptyBuffer(void)
{
    RingBuffer_t rb;
    uint8_t storage[4];
    uint8_t data = 0U;
    uint8_t pushValue = 0x11U;

    printf("\nringBufferIsEmpty / ringBufferPop on an empty buffer:\n");

    ringBufferInit(&rb, storage, sizeof(storage), sizeof(uint8_t));

    TEST_ASSERT(ringBufferIsEmpty(&rb) == true, "freshly initialized buffer is empty");

    TEST_ASSERT(ringBufferPop(&rb, &data) == RING_BUFFER_ERROR_EMPTY, "pop on an empty buffer returns RING_BUFFER_ERROR_EMPTY");
    TEST_ASSERT(ringBufferPeek(&rb, &data) == RING_BUFFER_ERROR_EMPTY, "peek on an empty buffer returns RING_BUFFER_ERROR_EMPTY");

    ringBufferPush(&rb, &pushValue);
    TEST_ASSERT(ringBufferIsEmpty(&rb) == false, "buffer is not empty after a push");
}

static void testPeekDoesNotRemove(void)
{
    RingBuffer_t rb;
    uint8_t storage[4];
    uint8_t pushValue = 0x42U;
    uint8_t peeked = 0U;
    uint8_t popped = 0U;
    size_t size = 0U;

    printf("\nringBufferPeek:\n");

    ringBufferInit(&rb, storage, sizeof(storage), sizeof(uint8_t));
    ringBufferPush(&rb, &pushValue);

    TEST_ASSERT(ringBufferPeek(&rb, &peeked) == RING_BUFFER_SUCCESS, "peek on a non-empty buffer succeeds");
    TEST_ASSERT(peeked == 0x42U, "peeked value matches the front of the buffer");

    ringBufferGetSize(&rb, &size);
    TEST_ASSERT(size == 1U, "size is unchanged after a peek (peek does not remove data)");

    ringBufferPop(&rb, &popped);
    TEST_ASSERT(popped == peeked, "the value popped afterward matches what was peeked");
}

static void testClear(void)
{
    RingBuffer_t rb;
    uint8_t storage[4];
    uint8_t one = 1U, two = 2U, nine = 9U;
    size_t size = 0U;

    printf("\nringBufferClear:\n");

    ringBufferInit(&rb, storage, sizeof(storage), sizeof(uint8_t));
    ringBufferPush(&rb, &one);
    ringBufferPush(&rb, &two);

    TEST_ASSERT(ringBufferClear(&rb) == RING_BUFFER_SUCCESS, "clear on a non-empty buffer succeeds");
    TEST_ASSERT(ringBufferIsEmpty(&rb) == true, "buffer is empty after clear");

    ringBufferGetSize(&rb, &size);
    TEST_ASSERT(size == 0U, "size is 0 after clear");

    TEST_ASSERT(ringBufferPush(&rb, &nine) == RING_BUFFER_SUCCESS, "buffer is usable again after clear");
}

static void testGetSize(void)
{
    RingBuffer_t rb;
    uint8_t storage[8];
    uint8_t one = 1U, two = 2U, three = 3U;
    uint8_t data = 0U;
    size_t size = 0U;

    printf("\nringBufferGetSize:\n");

    ringBufferInit(&rb, storage, sizeof(storage), sizeof(uint8_t));

    TEST_ASSERT(ringBufferGetSize(&rb, &size) == RING_BUFFER_SUCCESS, "getSize on a valid buffer succeeds");
    TEST_ASSERT(size == 0U, "size of a freshly initialized buffer is 0");

    ringBufferPush(&rb, &one);
    ringBufferPush(&rb, &two);
    ringBufferPush(&rb, &three);
    ringBufferGetSize(&rb, &size);
    TEST_ASSERT(size == 3U, "size reflects the number of pushed elements");

    ringBufferPop(&rb, &data);
    ringBufferGetSize(&rb, &size);
    TEST_ASSERT(size == 2U, "size decreases after a pop");

    TEST_ASSERT(ringBufferGetSize(NULL, &size) == RING_BUFFER_ERROR_NULL_POINTER,
                "getSize with NULL ringBuffer pointer returns RING_BUFFER_ERROR_NULL_POINTER");
    TEST_ASSERT(ringBufferGetSize(&rb, NULL) == RING_BUFFER_ERROR_NULL_POINTER,
                "getSize with NULL size pointer returns RING_BUFFER_ERROR_NULL_POINTER");
}

static void testGetCapacity(void)
{
    RingBuffer_t rb;
    uint8_t storage[8];
    size_t capacity = 0U;

    printf("\nringBufferGetCapacity:\n");

    ringBufferInit(&rb, storage, sizeof(storage), sizeof(uint8_t));

    TEST_ASSERT(ringBufferGetCapacity(&rb, &capacity) == RING_BUFFER_SUCCESS, "getCapacity on a valid buffer succeeds");
    TEST_ASSERT(capacity == sizeof(storage) - 1U,
                "usable capacity is one less than the underlying storage size");

    TEST_ASSERT(ringBufferGetCapacity(NULL, &capacity) == RING_BUFFER_ERROR_NULL_POINTER,
                "getCapacity with NULL ringBuffer pointer returns RING_BUFFER_ERROR_NULL_POINTER");
    TEST_ASSERT(ringBufferGetCapacity(&rb, NULL) == RING_BUFFER_ERROR_NULL_POINTER,
                "getCapacity with NULL capacity pointer returns RING_BUFFER_ERROR_NULL_POINTER");
}

static void testGetAvailableSpace(void)
{
    RingBuffer_t rb;
    uint8_t storage[4]; /* usable capacity 3 */
    uint8_t one = 1U;
    uint8_t data = 0U;
    size_t available = 0U;

    printf("\nringBufferGetAvailableSpace:\n");

    ringBufferInit(&rb, storage, sizeof(storage), sizeof(uint8_t));

    TEST_ASSERT(ringBufferGetAvailableSpace(&rb, &available) == RING_BUFFER_SUCCESS,
                "getAvailableSpace on a valid buffer succeeds");
    TEST_ASSERT(available == 3U, "empty buffer reports (capacity - 1) available slots");

    ringBufferPush(&rb, &one);
    ringBufferGetAvailableSpace(&rb, &available);
    TEST_ASSERT(available == 2U, "available space decreases by 1 after a push");

    ringBufferPop(&rb, &data);
    ringBufferGetAvailableSpace(&rb, &available);
    TEST_ASSERT(available == 3U, "available space increases by 1 after a pop");

    TEST_ASSERT(ringBufferGetAvailableSpace(NULL, &available) == RING_BUFFER_ERROR_NULL_POINTER,
                "getAvailableSpace with NULL ringBuffer pointer returns RING_BUFFER_ERROR_NULL_POINTER");
    TEST_ASSERT(ringBufferGetAvailableSpace(&rb, NULL) == RING_BUFFER_ERROR_NULL_POINTER,
                "getAvailableSpace with NULL availableSpace pointer returns RING_BUFFER_ERROR_NULL_POINTER");
}

static void testSizePlusAvailableEqualsCapacity(void)
{
    RingBuffer_t rb;
    uint8_t storage[8]; /* usable capacity 7 */
    uint8_t values[] = {1U, 2U, 3U};
    size_t size = 0U, available = 0U, capacity = 0U;
    size_t i;

    printf("\ncross-check: size + availableSpace == usable capacity at every point:\n");

    ringBufferInit(&rb, storage, sizeof(storage), sizeof(uint8_t));
    ringBufferGetCapacity(&rb, &capacity);

    for (i = 0U; i < sizeof(values) / sizeof(values[0]); i++)
    {
        ringBufferPush(&rb, &values[i]);
        ringBufferGetSize(&rb, &size);
        ringBufferGetAvailableSpace(&rb, &available);
        TEST_ASSERT(size + available == capacity, "size + availableSpace == capacity holds after each push");
    }
}

static void testWraparound(void)
{
    RingBuffer_t rb;
    uint8_t storage[4]; /* usable capacity 3 */
    int wrappedCorrectly = 1;
    uint8_t nextValue = 0U;
    int cycle, i;

    printf("\nwraparound behavior (head/tail wrapping past the end of storage):\n");

    ringBufferInit(&rb, storage, sizeof(storage), sizeof(uint8_t));

    /* Fill, drain, and refill repeatedly so head/tail wrap around the
     * underlying storage several times, exercising the modulo wraparound
     * logic in push/pop. */
    for (cycle = 0; cycle < 5; cycle++)
    {
        for (i = 0; i < 3; i++)
        {
            if (ringBufferPush(&rb, &nextValue) != RING_BUFFER_SUCCESS)
            {
                wrappedCorrectly = 0;
            }
            nextValue++;
        }

        for (i = 0; i < 3; i++)
        {
            uint8_t data = 0U;
            if (ringBufferPop(&rb, &data) != RING_BUFFER_SUCCESS)
            {
                wrappedCorrectly = 0;
            }
        }
    }

    TEST_ASSERT(wrappedCorrectly, "repeated fill/drain cycles succeed across storage wraparound");
    TEST_ASSERT(ringBufferIsEmpty(&rb) == true, "buffer is empty after equal pushes and pops across wraparound");
}

static void testGenericElementType(void)
{
    /* Exercises the generic-element design: elementSize > 1, using a
     * multi-byte struct rather than a single uint8_t. This is the whole
     * reason RingBuffer_t carries elementSize instead of being byte-only. */
    typedef struct
    {
        uint16_t id;
        uint16_t value;
    } Sample_t;

    RingBuffer_t rb;
    Sample_t storage[4]; /* usable capacity 3 elements */
    Sample_t in1 = {1U, 100U};
    Sample_t in2 = {2U, 200U};
    Sample_t out = {0U, 0U};

    printf("\ngeneric element support (elementSize > 1, struct elements):\n");

    TEST_ASSERT(ringBufferInit(&rb, storage, 4U, sizeof(Sample_t)) == RING_BUFFER_SUCCESS,
                "init with a multi-byte struct element type succeeds");

    TEST_ASSERT(ringBufferPush(&rb, &in1) == RING_BUFFER_SUCCESS, "push a struct element succeeds");
    TEST_ASSERT(ringBufferPush(&rb, &in2) == RING_BUFFER_SUCCESS, "push a second struct element succeeds");

    TEST_ASSERT(ringBufferPop(&rb, &out) == RING_BUFFER_SUCCESS, "pop a struct element succeeds");
    TEST_ASSERT((out.id == 1U) && (out.value == 100U), "popped struct matches the first struct pushed (FIFO)");

    ringBufferPop(&rb, &out);
    TEST_ASSERT((out.id == 2U) && (out.value == 200U), "second popped struct matches the second struct pushed");
}

static void testNullPointerHandling(void)
{
    RingBuffer_t rb;
    uint8_t storage[4];
    uint8_t data = 0U;
    uint8_t pushValue = 1U;

    printf("\nnegative: NULL pointer handling across the API:\n");

    ringBufferInit(&rb, storage, sizeof(storage), sizeof(uint8_t));

    TEST_ASSERT(ringBufferPush(NULL, &pushValue) == RING_BUFFER_ERROR_NULL_POINTER,
                "push with NULL ringBuffer pointer returns RING_BUFFER_ERROR_NULL_POINTER");
    TEST_ASSERT(ringBufferPush(&rb, NULL) == RING_BUFFER_ERROR_NULL_POINTER,
                "push with NULL data pointer returns RING_BUFFER_ERROR_NULL_POINTER");
    TEST_ASSERT(ringBufferPop(NULL, &data) == RING_BUFFER_ERROR_NULL_POINTER,
                "pop with NULL ringBuffer pointer returns RING_BUFFER_ERROR_NULL_POINTER");
    TEST_ASSERT(ringBufferPop(&rb, NULL) == RING_BUFFER_ERROR_NULL_POINTER,
                "pop with NULL data pointer returns RING_BUFFER_ERROR_NULL_POINTER");
    TEST_ASSERT(ringBufferPeek(NULL, &data) == RING_BUFFER_ERROR_NULL_POINTER,
                "peek with NULL ringBuffer pointer returns RING_BUFFER_ERROR_NULL_POINTER");
    TEST_ASSERT(ringBufferPeek(&rb, NULL) == RING_BUFFER_ERROR_NULL_POINTER,
                "peek with NULL data pointer returns RING_BUFFER_ERROR_NULL_POINTER");
    TEST_ASSERT(ringBufferClear(NULL) == RING_BUFFER_ERROR_NULL_POINTER,
                "clear with NULL ringBuffer pointer returns RING_BUFFER_ERROR_NULL_POINTER");

    TEST_ASSERT(ringBufferIsFull(NULL) == false, "isFull with NULL ringBuffer pointer returns false");
    TEST_ASSERT(ringBufferIsEmpty(NULL) == true, "isEmpty with NULL ringBuffer pointer returns true");
}

int main(void)
{
    printf("Running ringBuffer tests...\n");

    testInit();
    testPushPopBasic();
    testPushPopOrderingFIFO();
    testFullBuffer();
    testEmptyBuffer();
    testPeekDoesNotRemove();
    testClear();
    testGetSize();
    testGetCapacity();
    testGetAvailableSpace();
    testSizePlusAvailableEqualsCapacity();
    testWraparound();
    testGenericElementType();
    testNullPointerHandling();

    printf("\n%d / %d tests passed.\n", passedTests, totalTests);

    return (passedTests == totalTests) ? 0 : 1;
}

/**************************************** END OF testRingBuffer.c ****************************************/