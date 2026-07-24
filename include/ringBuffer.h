/******************************************************************************
 * @file    ringBuffer.h
 * @brief   Public interface for a generic ring buffer.
 *
 * @author  Abhishek Doddagoudar
 * @date    July 2026
 *
 * @details
 * Implements a reusable fixed-size ring buffer suitable for embedded systems.
 *
 * Features:
 *  - Fixed-size buffer
 *  - No dynamic memory allocation
 *  - Caller-owned storage
 *  - Reentrant
 *  - Generic element support
 *  - FIFO semantics
 *
 * Current Implementation:
 *  - Uses the one-empty-slot technique to distinguish full and empty states.
 *  - Maximum usable capacity is (capacity - 1) elements.
 *
 * Thread Safety
 *  - This module is reentrant.
 *  - Concurrent access to the same ring buffer instance must be synchronized by the caller.
 *
 ******************************************************************************/

#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Ring buffer object.
 *
 * The caller owns both this object and the backing storage.
 */
typedef struct
{
    uint8_t *buffer;       /**< Pointer to backing storage.            */
    size_t head;        /**< Next write position.                   */
    size_t tail;        /**< Next read position.                    */
    size_t capacity;    /**< Capacity in elements.                  */
    size_t elementSize; /**< Size of one element in bytes.          */

} RingBuffer_t;

/**
 * @brief Ring buffer status codes.
 */
typedef enum
{
    RING_BUFFER_SUCCESS = 0,
    RING_BUFFER_ERROR_NULL_POINTER,
    RING_BUFFER_ERROR_INVALID_CAPACITY,
    RING_BUFFER_ERROR_INVALID_ELEMENT_SIZE,
    RING_BUFFER_ERROR_FULL,
    RING_BUFFER_ERROR_EMPTY
} RingBufferStatus_t;

/**
 * @brief Initialize a ring buffer.
 *
 * @param ringBuffer   Ring buffer object.
 * @param storage      Caller-provided storage.
 * @param capacity     Number of elements.
 * @param elementSize  Size of each element in bytes.
 *
 * @return Operation status.
 */
RingBufferStatus_t ringBufferInit(RingBuffer_t *ringBuffer, void *storage, size_t capacity, size_t elementSize);

/**
 * @brief Insert one element.
 *
 * @param ringBuffer Ring buffer.
 * @param data       Pointer to one element.
 *
 * @return Operation status.
 */
RingBufferStatus_t ringBufferPush(RingBuffer_t *ringBuffer, const void *data);

/**
 * @brief Remove one element.
 *
 * @param ringBuffer Ring buffer.
 * @param data       Destination for removed element.
 *
 * @return Operation status.
 */
RingBufferStatus_t ringBufferPop(RingBuffer_t *ringBuffer, void *data);

/**
 * @brief Read the next element without removing it.
 *
 * @param ringBuffer Ring buffer.
 * @param data       Destination buffer.
 *
 * @return Operation status.
 */
RingBufferStatus_t ringBufferPeek(const RingBuffer_t *ringBuffer, void *data);

/**
 * @brief Remove all elements.
 *
 * @param ringBuffer Ring buffer.
 *
 * @return Operation status.
 */
RingBufferStatus_t ringBufferClear(RingBuffer_t *ringBuffer);

/**
 * @brief Determine whether the buffer is empty.
 *
 * @param ringBuffer Ring buffer.
 *
 * @return true if empty, otherwise false.
 */
bool ringBufferIsEmpty(const RingBuffer_t *ringBuffer);

/**
 * @brief Determine whether the buffer is full.
 *
 * @param ringBuffer Ring buffer.
 *
 * @return true if full, otherwise false.
 */
bool ringBufferIsFull(const RingBuffer_t *ringBuffer);

/**
 * @brief Get the number of stored elements.
 *
 * @param ringBuffer Ring buffer.
 * @param size       Number of elements currently stored.
 *
 * @return Operation status.
 */
RingBufferStatus_t ringBufferGetSize(const RingBuffer_t *ringBuffer, size_t *size);

/**
 * @brief Get the usable capacity.
 *
 * @param ringBuffer Ring buffer.
 * @param capacity   Maximum number of elements that can be stored.
 *
 * @return Operation status.
 */
RingBufferStatus_t ringBufferGetCapacity(const RingBuffer_t *ringBuffer, size_t *capacity);

/**
 * @brief Get the number of elements that can currently be pushed before the ring buffer is full.
 *
 * @details Reflects the current head/tail state at the time of the call; the value is a
 *          snapshot and may become stale immediately if another push or pop occurs
 *          afterward (this module provides no internal locking -- see Thread Safety).
 *
 * @param ringBuffer Pointer to the ring buffer structure.
 * @param availableSpace Pointer to store the number of free element slots.
 *
 * @return RingBufferStatus_t Status of the operation.
 */
RingBufferStatus_t ringBufferGetAvailableSpace(const RingBuffer_t *ringBuffer, size_t *availableSpace);

#endif

/******************************* END OF FILE **********************************/