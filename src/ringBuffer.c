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
 *
 * @param ringBuffer Pointer to the ring buffer structure.
 * @param buffer Pointer to caller-provided storage.
 * @param capacity Maximum number of bytes the ring buffer can store.
 * 
 * @exception If the ringBuffer or buffer pointers are NULL, or if capacity is zero, the function returns RING_BUFFER_NULL_POINTER.
 * 
 * @return RingBufferStatus_t Status of the operation.
 */
RingBufferStatus_t ringBufferInit(RingBuffer_t *ringBuffer, uint8_t *buffer, size_t capacity)
{
    if (ringBuffer == NULL || buffer == NULL || capacity == 0)
    {
        return RING_BUFFER_NULL_POINTER;
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
 * @exception If the ringBuffer pointer is NULL or if the buffer is full, the function returns an appropriate error code.
 * 
 * @return RingBufferStatus_t Status of the operation.
 */
RingBufferStatus_t ringBufferPush(RingBuffer_t *ringBuffer, uint8_t data)
{
    if (ringBuffer == NULL || ringBuffer->buffer == NULL)
    {
        return RING_BUFFER_NULL_POINTER;
    }

    size_t nextHead = (ringBuffer->head + 1) % ringBuffer->capacity;

    if (nextHead == ringBuffer->tail)
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
 * @exception If the ringBuffer pointer is NULL or if the buffer is empty, the function returns an appropriate error code.
 * 
 * @return RingBufferStatus_t Status of the operation.
 */
RingBufferStatus_t ringBufferPop(RingBuffer_t *ringBuffer, uint8_t *data)
{
    if (ringBuffer == NULL || ringBuffer->buffer == NULL || data == NULL)
    {
        return RING_BUFFER_NULL_POINTER;
    }

    if (ringBuffer->head == ringBuffer->tail)
    {
        return RING_BUFFER_EMPTY;
    }

    *data = ringBuffer->buffer[ringBuffer->tail];
    ringBuffer->tail = (ringBuffer->tail + 1) % ringBuffer->capacity;

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
 * @exception If the ringBuffer pointer is NULL or if the buffer is empty, the function returns an appropriate error code.
 * 
 * @return RingBufferStatus_t Status of the operation.
 */
RingBufferStatus_t ringBufferPeek(const RingBuffer_t *ringBuffer, uint8_t *data)
{
    if (ringBuffer == NULL || ringBuffer->buffer == NULL || data == NULL)
    {
        return RING_BUFFER_NULL_POINTER;
    }

    if (ringBuffer->head == ringBuffer->tail)
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
 * @exception If the ringBuffer pointer is NULL, the function returns RING_BUFFER_NULL_POINTER.
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
 * @exception If the ringBuffer pointer is NULL or if the size pointer is NULL, the function returns RING_BUFFER_NULL_POINTER.
 * 
 * @return RingBufferStatus_t Status of the operation.
 */
RingBufferStatus_t ringBufferGetSize(const RingBuffer_t *ringBuffer, size_t *size)
{
    if (ringBuffer == NULL || size == NULL)
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
 * @exception If the ringBuffer pointer is NULL or if the capacity pointer is NULL, the function returns RING_BUFFER_NULL_POINTER.
 * 
 * @return RingBufferStatus_t Status of the operation.
 */
RingBufferStatus_t ringBufferGetCapacity(const RingBuffer_t *ringBuffer, size_t *capacity)
{
    if (ringBuffer == NULL || capacity == NULL)
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
 * @exception If the ringBuffer pointer is NULL, the function returns false.
 * 
 * @return true if the ring buffer is full, false otherwise.
 */
bool ringBufferIsFull(const RingBuffer_t *ringBuffer)
{
    if (ringBuffer == NULL)
    {
        return false;
    }

    size_t nextHead = (ringBuffer->head + 1) % ringBuffer->capacity;
    return nextHead == ringBuffer->tail;
}

/**
 * @brief Check if the ring buffer is empty.
 * 
 * @details This function checks if the ring buffer is empty.
 *
 * @param ringBuffer Pointer to the ring buffer structure.
 * 
 * @exception If the ringBuffer pointer is NULL, the function returns true.
 * 
 * @return true if the ring buffer is empty, false otherwise.
 */
bool ringBufferIsEmpty(const RingBuffer_t *ringBuffer)
{
    if (ringBuffer == NULL)
    {
        return true;
    }

    return ringBuffer->head == ringBuffer->tail;
}

/**********************************************END OF ringBuffer.c**********************************/