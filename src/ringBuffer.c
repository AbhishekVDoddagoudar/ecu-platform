/******************************************************************************
 * @file ringBuffer.c
 * @brief Implementation of ring buffer utilities.
 *
 * @author Abhishek Doddagoudar
 * @date July 2026
 ******************************************************************************/

#include <ringBuffer.h>

/**
 * @brief Initialize the ring buffer.
 *
 * @details This function initializes the ring buffer with the provided buffer memory and capacity.
 *          Minimum capacity must be 2 to distinguish between full and empty states.
 *
 * @param ringBuffer Pointer to the ring buffer structure.
 * @param buffer Pointer to caller-provided storage.
 * @param capacity Maximum number of bytes the ring buffer can store.
 *
 * @retval RING_BUFFER_OK if initialization is successful.
 * @retval RING_BUFFER_INVALID_PARAMETER if the parameters are invalid.
 *
 * @return RingBufferStatus_t Status of the operation.
 */
RingBufferStatus_t ringBufferInit(RingBuffer_t *ringBuffer, uint8_t *buffer, size_t capacity)
{
    if (ringBuffer == NULL || buffer == NULL || capacity < 2U)
    {
        return RING_BUFFER_INVALID_PARAMETER;
    }

    ringBuffer->buffer = buffer;
    ringBuffer->capacity = capacity;
    ringBuffer->head = 0;
    ringBuffer->tail = 0;

    return RING_BUFFER_OK;
}

/**
 * @brief Push data into the ring buffer.
 *
 * @details This function adds a byte of data to the ring buffer. If the buffer is full, it returns an error.
 *
 * @param ringBuffer Pointer to the ring buffer structure.
 * @param data Data to be pushed.
 *
 * @retval RING_BUFFER_OK if the data is successfully pushed.
 * @retval RING_BUFFER_FULL if the buffer is full.
 * @retval RING_BUFFER_NULL_POINTER if the parameters are invalid.
 *
 * @return RingBufferStatus_t Status of the operation.
 */
RingBufferStatus_t ringBufferPush(RingBuffer_t *ringBuffer, uint8_t data)
{
    if (ringBuffer == NULL || ringBuffer->buffer == NULL)
    {
        return RING_BUFFER_NULL_POINTER;
    }

    size_t nextHead = (ringBuffer->head + 1U) % ringBuffer->capacity;

    if (ringBufferIsFull(ringBuffer))
    {
        return RING_BUFFER_FULL;
    }

    ringBuffer->buffer[ringBuffer->head] = data;
    ringBuffer->head = nextHead;

    return RING_BUFFER_OK;
}

/**
 * @brief Pop data from the ring buffer.
 *
 * @details This function removes a byte of data from the ring buffer. If the buffer is empty, it returns an error.
 *
 * @param ringBuffer Pointer to the ring buffer structure.
 * @param data Pointer to the data to be popped.
 *
 * @retval RING_BUFFER_OK if the data is successfully popped.
 * @retval RING_BUFFER_EMPTY if the buffer is empty.
 * @retval RING_BUFFER_NULL_POINTER if the parameters are invalid.
 *
 * @return RingBufferStatus_t Status of the operation.
 */
RingBufferStatus_t ringBufferPop(RingBuffer_t *ringBuffer, uint8_t *data)
{
    if (ringBuffer == NULL || ringBuffer->buffer == NULL || data == NULL)
    {
        return RING_BUFFER_NULL_POINTER;
    }

    if (ringBufferIsEmpty(ringBuffer))
    {
        return RING_BUFFER_EMPTY;
    }

    *data = ringBuffer->buffer[ringBuffer->tail];
    ringBuffer->tail = (ringBuffer->tail + 1U) % ringBuffer->capacity;

    return RING_BUFFER_OK;
}

/**
 * @brief Peek at the data in the ring buffer.
 *
 * @details This function allows viewing the data at the front of the ring buffer without removing it. If the buffer is empty, it returns an error.
 *
 * @param ringBuffer Pointer to the ring buffer structure.
 * @param data Pointer to the data to be peeked.
 *
 * @retval RING_BUFFER_OK if the data is successfully peeked.
 * @retval RING_BUFFER_EMPTY if the buffer is empty.
 * @retval RING_BUFFER_NULL_POINTER if the parameters are invalid.
 *
 * @return RingBufferStatus_t Status of the operation.
 */
RingBufferStatus_t ringBufferPeek(const RingBuffer_t *ringBuffer, uint8_t *data)
{
    if (ringBuffer == NULL || ringBuffer->buffer == NULL || data == NULL)
    {
        return RING_BUFFER_NULL_POINTER;
    }

    if (ringBufferIsEmpty(ringBuffer))
    {
        return RING_BUFFER_EMPTY;
    }

    *data = ringBuffer->buffer[ringBuffer->tail];

    return RING_BUFFER_OK;
}

/**
 * @brief Clear the ring buffer.
 *
 * @details This function clears all data from the ring buffer.
 *
 * @param ringBuffer Pointer to the ring buffer structure.
 *
 * @retval RING_BUFFER_OK if the buffer is successfully cleared.
 * @retval RING_BUFFER_NULL_POINTER if the parameters are invalid.
 *
 * @return RingBufferStatus_t Status of the operation.
 */
RingBufferStatus_t ringBufferClear(RingBuffer_t *ringBuffer)
{
    if (ringBuffer == NULL || ringBuffer->buffer == NULL)
    {
        return RING_BUFFER_NULL_POINTER;
    }

    ringBuffer->head = 0;
    ringBuffer->tail = 0;

    return RING_BUFFER_OK;
}

/**
 * @brief Get the size of the ring buffer.
 *
 * @details This function returns the number of elements currently in the ring buffer.
 *
 * @param ringBuffer Pointer to the ring buffer structure.
 * @param size Pointer to the variable to store the size.
 *
 * @retval RING_BUFFER_OK if the size is successfully retrieved.
 * @retval RING_BUFFER_NULL_POINTER if the parameters are invalid.
 *
 * @return RingBufferStatus_t Status of the operation.
 */
RingBufferStatus_t ringBufferGetSize(const RingBuffer_t *ringBuffer, size_t *size)
{
    if (ringBuffer == NULL || ringBuffer->buffer == NULL || size == NULL)
    {
        return RING_BUFFER_NULL_POINTER;
    }

    if (ringBuffer->head >= ringBuffer->tail)
    {
        *size = ringBuffer->head - ringBuffer->tail;
    }
    else
    {
        *size = ringBuffer->capacity - (ringBuffer->tail - ringBuffer->head);
    }

    return RING_BUFFER_OK;
}

/**
 * @brief Get the capacity of the ring buffer.
 *
 * @details This function returns the maximum number of elements the ring buffer can hold.
 *
 * @param ringBuffer Pointer to the ring buffer structure.
 * @param capacity Pointer to the variable to store the capacity.
 *
 * @retval RING_BUFFER_OK if the capacity is successfully retrieved.
 * @retval RING_BUFFER_NULL_POINTER if the parameters are invalid.
 *
 * @return RingBufferStatus_t Status of the operation.
 */
RingBufferStatus_t ringBufferGetCapacity(const RingBuffer_t *ringBuffer, size_t *capacity)
{
    if (ringBuffer == NULL || ringBuffer->buffer == NULL || capacity == NULL)
    {
        return RING_BUFFER_NULL_POINTER;
    }

    *capacity = ringBuffer->capacity - 1; // One slot is reserved to distinguish full from empty

    return RING_BUFFER_OK;
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

    size_t nextHead = (ringBuffer->head + 1U) % ringBuffer->capacity;

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

/**********************************************END OF ringBuffer.c**********************************/