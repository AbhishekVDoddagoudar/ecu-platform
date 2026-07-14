/******************************************************************************
 * @file ringBuffer.h
 * @brief Public interface for ring buffer utilities.
 *
 * @author Abhishek Doddagoudar
 * @date July 2026
 ******************************************************************************/

/**
 * Implementation Notes:
 *
 * This ring buffer uses the one-empty-slot technique to distinguish
 * between full and empty states.
 *
 * A buffer of capacity N can store a maximum of (N - 1) bytes.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct
{
    uint8_t *buffer; /**< Pointer to the buffer memory. */
    size_t capacity; /**< Maximum number of elements the buffer can hold. */
    size_t head;     /**< Index of the head of the buffer. */
    size_t tail;     /**< Index of the tail of the buffer. */
} RingBuffer_t;

typedef enum
{
    RING_BUFFER_OK = 0,           /**< Operation successful. */
    RING_BUFFER_NULL_POINTER,     /**< Null pointer error. */
    RING_BUFFER_FULL,             /**< Buffer is full. */
    RING_BUFFER_EMPTY,            /**< Buffer is empty. */
    RING_BUFFER_INVALID_PARAMETER /**< General error. */
} RingBufferStatus_t;

/**
 * @brief Initialize the ring buffer.
 *
 * @param ringBuffer Pointer to the ring buffer structure.
 * @param buffer Pointer to the buffer memory.
 * @param capacity Maximum number of bytes the ring buffer can store.
 * @return RingBufferStatus_t Status of the operation.
 */
RingBufferStatus_t ringBufferInit(RingBuffer_t *ringBuffer, uint8_t *buffer, size_t capacity);

/**
 * @brief Push data into the ring buffer.
 *
 * @param ringBuffer Pointer to the ring buffer structure.
 * @param data Data to be pushed.
 * @return RingBufferStatus_t Status of the operation.
 */
RingBufferStatus_t ringBufferPush(RingBuffer_t *ringBuffer, uint8_t data);

/**
 * @brief Pop data from the ring buffer.
 *
 * @param ringBuffer Pointer to the ring buffer structure.
 * @param data Pointer to the data to be popped.
 * @return RingBufferStatus_t Status of the operation.
 */
RingBufferStatus_t ringBufferPop(RingBuffer_t *ringBuffer, uint8_t *data);

/**
 * @brief Peek at the data in the ring buffer.
 *
 * @param ringBuffer Pointer to the ring buffer structure.
 * @param data Pointer to the data to be peeked.
 * @return RingBufferStatus_t Status of the operation.
 */
RingBufferStatus_t ringBufferPeek(const RingBuffer_t *ringBuffer, uint8_t *data);

/**
 * @brief Clear the ring buffer.
 *
 * @param ringBuffer Pointer to the ring buffer structure.
 * @return RingBufferStatus_t Status of the operation.
 */
RingBufferStatus_t ringBufferClear(RingBuffer_t *ringBuffer);

/**
 * @brief Check if the ring buffer is full.
 *
 * @param ringBuffer Pointer to the ring buffer structure.
 * @return true if the buffer is full, false otherwise.
 */
bool ringBufferIsFull(const RingBuffer_t *ringBuffer);

/**
 * @brief Check if the ring buffer is empty.
 *
 * @param ringBuffer Pointer to the ring buffer structure.
 * @return true if the buffer is empty, false otherwise.
 */
bool ringBufferIsEmpty(const RingBuffer_t *ringBuffer);

/**
 * @brief Get the current capacity of the ring buffer.
 *
 * @param ringBuffer Pointer to the ring buffer structure.
 * @param capacity Pointer to store the current capacity.
 * @return RingBufferStatus_t Status of the operation.
 */
RingBufferStatus_t ringBufferGetSize(const RingBuffer_t *ringBuffer, size_t *capacity);

/**
 * @brief Get the capacity of the ring buffer.
 *
 * @param ringBuffer Pointer to the ring buffer structure.
 * @param capacity Pointer to store the capacity.
 * @return RingBufferStatus_t Status of the operation.
 */
RingBufferStatus_t ringBufferGetCapacity(const RingBuffer_t *ringBuffer, size_t *capacity);

/********************************************END OF ringBuffer.h************************************/