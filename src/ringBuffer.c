/******************************************************************************
 * @file ringBuffer.c
 * @brief Implementation of ring buffer utilities.
 *
 * @author Abhishek Doddagoudar
 * @date July 2026
 ******************************************************************************/

#include <ringBuffer.h>
#include <string.h>


/**
 * @internal
 * @brief Advances a ring buffer index by one position, wrapping at capacity.
 *
 * @param index    Current index (head or tail).
 * @param capacity Ring buffer capacity.
 *
 * @return size_t The next index, wrapped to [0, capacity).
 */
static inline size_t ringBufferNextIndex(size_t index, size_t capacity)
{
    return (index + 1U) % capacity;
}

/**
 * @brief Initialize the ring buffer.
 *
 * @details This function initializes the ring buffer with the provided buffer memory and capacity.
 *          Minimum capacity must be 2 to distinguish between full and empty states.
 *
 * @param ringBuffer Pointer to the ring buffer structure.
 * @param buffer Pointer to caller-provided storage.
 * @param capacity Maximum number of elements the ring buffer can store.
 * @param elementSize Size of each element in bytes.
 *
 * @return RingBufferStatus_t Status of the operation.
 *
 * @retval RING_BUFFER_SUCCESS Initialization successful.
 * @retval RING_BUFFER_ERROR_NULL_POINTER ringBuffer or buffer is NULL.
 * @retval RING_BUFFER_ERROR_INVALID_CAPACITY capacity is less than 2 (need at least 2 slots to distinguish full from empty).
 * @retval RING_BUFFER_ERROR_INVALID_ELEMENT_SIZE elementSize is 0.
 */
RingBufferStatus_t ringBufferInit(RingBuffer_t *ringBuffer, void *buffer, size_t capacity, size_t elementSize)
{
    if (ringBuffer == NULL || buffer == NULL)
    {
        return RING_BUFFER_ERROR_NULL_POINTER;
    }

    if (capacity < 2U)
    {
        return RING_BUFFER_ERROR_INVALID_CAPACITY;
    }

    if (elementSize < 1U)
    {
        return RING_BUFFER_ERROR_INVALID_ELEMENT_SIZE;
    }

    ringBuffer->buffer = (uint8_t *)buffer;
    ringBuffer->capacity = capacity;
    ringBuffer->elementSize = elementSize;
    ringBuffer->head = 0;
    ringBuffer->tail = 0;

    return RING_BUFFER_SUCCESS;
}

/**
 * @brief Push an element into the ring buffer.
 *
 * @details Copies elementSize bytes from data into the buffer. If the
 *          buffer is full, no data is copied and an error is returned.
 *
 * @param ringBuffer Pointer to the ring buffer structure.
 * @param data Pointer to the element to copy in (must be at least
 *        ringBuffer->elementSize bytes).
 *
 * @return RingBufferStatus_t Status of the operation.
 *
 * @retval RING_BUFFER_SUCCESS Element pushed successfully.
 * @retval RING_BUFFER_ERROR_FULL Buffer is full; nothing was copied.
 * @retval RING_BUFFER_ERROR_NULL_POINTER ringBuffer, ringBuffer->buffer, or data is NULL.
 */
RingBufferStatus_t ringBufferPush(RingBuffer_t *ringBuffer, const void *data)
{
    if ((ringBuffer == NULL) || (ringBuffer->buffer == NULL) || (data == NULL))
    {
        return RING_BUFFER_ERROR_NULL_POINTER;
    }

    if (ringBufferIsFull(ringBuffer))
    {
        return RING_BUFFER_ERROR_FULL;
    }

    uint8_t *dest = ringBuffer->buffer + (ringBuffer->head * ringBuffer->elementSize);
    memcpy(dest, data, ringBuffer->elementSize);

    ringBuffer->head = ringBufferNextIndex(ringBuffer->head, ringBuffer->capacity);

    return RING_BUFFER_SUCCESS;
}

/**
 * @brief Pop an element from the ring buffer.
 *
 * @details Copies elementSize bytes from the buffer into data. If the
 *          buffer is empty, no data is copied and an error is returned.
 *
 * @param ringBuffer Pointer to the ring buffer structure.
 * @param data Pointer to store the popped element (must be at least
 *        ringBuffer->elementSize bytes).
 *
 * @return RingBufferStatus_t Status of the operation.
 *
 * @retval RING_BUFFER_SUCCESS Element popped successfully.
 * @retval RING_BUFFER_ERROR_EMPTY Buffer is empty; nothing was copied.
 * @retval RING_BUFFER_ERROR_NULL_POINTER ringBuffer, ringBuffer->buffer, or data is NULL.
 */
RingBufferStatus_t ringBufferPop(RingBuffer_t *ringBuffer, void *data)
{
    if ((ringBuffer == NULL) || (ringBuffer->buffer == NULL) || (data == NULL))
    {
        return RING_BUFFER_ERROR_NULL_POINTER;
    }

    if (ringBufferIsEmpty(ringBuffer))
    {
        return RING_BUFFER_ERROR_EMPTY;
    }

    const uint8_t *src = ringBuffer->buffer + (ringBuffer->tail * ringBuffer->elementSize);
    memcpy(data, src, ringBuffer->elementSize);

    ringBuffer->tail = ringBufferNextIndex(ringBuffer->tail, ringBuffer->capacity);

    return RING_BUFFER_SUCCESS;
}

/**
 * @brief Peek at the data in the ring buffer.
 *
 * @details This function allows viewing the data at the front of the ring buffer without removing it. If the buffer is empty, it returns an error.
 *
 * @param ringBuffer Pointer to the ring buffer structure.
 * @param data Pointer to the data to be peeked.
 *
 * @retval RING_BUFFER_SUCCESS if the data is successfully peeked.
 * @retval RING_BUFFER_ERROR_EMPTY if the buffer is empty.
 * @retval RING_BUFFER_ERROR_NULL_POINTER if the parameters are invalid.
 *
 * @return RingBufferStatus_t Status of the operation.
 */
RingBufferStatus_t ringBufferPeek(const RingBuffer_t *ringBuffer, void *data)
{
    if (ringBuffer == NULL || ringBuffer->buffer == NULL || data == NULL)
    {
        return RING_BUFFER_ERROR_NULL_POINTER;
    }

    if (ringBufferIsEmpty(ringBuffer))
    {
        return RING_BUFFER_ERROR_EMPTY;
    }

    const uint8_t *src = (const uint8_t *)ringBuffer->buffer + (ringBuffer->tail * ringBuffer->elementSize);
    memcpy(data, src, ringBuffer->elementSize);

    return RING_BUFFER_SUCCESS;
}

/**
 * @brief Clear the ring buffer.
 *
 * @details This function clears all data from the ring buffer.
 *
 * @param ringBuffer Pointer to the ring buffer structure.
 *
 * @retval RING_BUFFER_SUCCESS if the buffer is successfully cleared.
 * @retval RING_BUFFER_ERROR_NULL_POINTER if the parameters are invalid.
 *
 * @return RingBufferStatus_t Status of the operation.
 */
RingBufferStatus_t ringBufferClear(RingBuffer_t *ringBuffer)
{
    if (ringBuffer == NULL || ringBuffer->buffer == NULL)
    {
        return RING_BUFFER_ERROR_NULL_POINTER;
    }

    ringBuffer->head = 0;
    ringBuffer->tail = 0;

    return RING_BUFFER_SUCCESS;
}

/**
 * @brief Get the size of the ring buffer.
 *
 * @details This function returns the number of elements currently in the ring buffer.
 *
 * @param ringBuffer Pointer to the ring buffer structure.
 * @param size Pointer to the variable to store the size.
 *
 * @retval RING_BUFFER_SUCCESS if the size is successfully retrieved.
 * @retval RING_BUFFER_ERROR_NULL_POINTER if the parameters are invalid.
 *
 * @return RingBufferStatus_t Status of the operation.
 */
RingBufferStatus_t ringBufferGetSize(const RingBuffer_t *ringBuffer, size_t *size)
{
    if (ringBuffer == NULL || ringBuffer->buffer == NULL || size == NULL)
    {
        return RING_BUFFER_ERROR_NULL_POINTER;
    }

    if (ringBuffer->head >= ringBuffer->tail)
    {
        *size = ringBuffer->head - ringBuffer->tail;
    }
    else
    {
        *size = ringBuffer->capacity - (ringBuffer->tail - ringBuffer->head);
    }

    return RING_BUFFER_SUCCESS;
}

/**
 * @brief Get the capacity of the ring buffer.
 *
 * @details This function returns the maximum number of elements the ring buffer can hold.
 *
 * @param ringBuffer Pointer to the ring buffer structure.
 * @param capacity Pointer to the variable to store the capacity.
 *
 * @retval RING_BUFFER_SUCCESS if the capacity is successfully retrieved.
 * @retval RING_BUFFER_ERROR_NULL_POINTER if the parameters are invalid.
 *
 * @return RingBufferStatus_t Status of the operation.
 */
RingBufferStatus_t ringBufferGetCapacity(const RingBuffer_t *ringBuffer, size_t *capacity)
{
    if (ringBuffer == NULL || ringBuffer->buffer == NULL || capacity == NULL)
    {
        return RING_BUFFER_ERROR_NULL_POINTER;
    }

    *capacity = ringBuffer->capacity - 1; // One slot is reserved to distinguish full from empty

    return RING_BUFFER_SUCCESS;
}

/**
 * @brief Check if the ring buffer is full.
 *
 * @details This function checks if the ring buffer is full.
 *
 * @param ringBuffer Pointer to the ring buffer structure.
 *
 * @return true if the ring buffer is full, false otherwise.
 */
bool ringBufferIsFull(const RingBuffer_t *ringBuffer)
{
    if (ringBuffer == NULL || ringBuffer->buffer == NULL)
    {
        return false;
    }

    size_t nextHead = ringBufferNextIndex(ringBuffer->head, ringBuffer->capacity);

    return (nextHead == ringBuffer->tail);
}
/**
 * @brief Check if the ring buffer is empty.
 *
 * @details This function checks if the ring buffer is empty.
 *
 * @param ringBuffer Pointer to the ring buffer structure.
 *
 * @return true if the ring buffer is empty, false otherwise.
 */
bool ringBufferIsEmpty(const RingBuffer_t *ringBuffer)
{
    if (ringBuffer == NULL || ringBuffer->buffer == NULL)
    {
        return true;
    }

    return (ringBuffer->head == ringBuffer->tail);
}

/**
 * @brief Get the number of elements that can currently be pushed before the ring buffer is full.
 *
 * @details Reflects the current head/tail state at the time of the call; the value is a
 *          snapshot and may become stale immediately if another push or pop occurs
 *          afterward (this module provides no internal locking -- see Thread Safety).
 *
 *          Uses the reserve-one-slot convention: usable capacity is (capacity - 1), so an
 *          empty buffer reports (capacity - 1) available slots, not capacity.
 *
 * @param ringBuffer Pointer to the ring buffer structure.
 * @param availableSpace Pointer to store the number of free element slots.
 *
 * @return RingBufferStatus_t Status of the operation.
 *
 * @retval RING_BUFFER_SUCCESS availableSpace has been populated.
 * @retval RING_BUFFER_ERROR_NULL_POINTER ringBuffer or availableSpace is NULL.
 */
RingBufferStatus_t ringBufferGetAvailableSpace(const RingBuffer_t *ringBuffer, size_t *availableSpace)
{
    if (ringBuffer == NULL || availableSpace == NULL)
    {
        return RING_BUFFER_ERROR_NULL_POINTER;
    }

    *availableSpace = ((ringBuffer->tail - ringBuffer->head - 1U) + ringBuffer->capacity) % ringBuffer->capacity;

    return RING_BUFFER_SUCCESS;
}

/**********************************************END OF ringBuffer.c**********************************/