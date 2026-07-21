/******************************************************************************
 * @file    bitUtils.h
 * @brief   Public interface for bit manipulation utilities.
 *
 * @author  Abhishek Doddagoudar
 *
 * @date    July 2026
 *
 * Provides reusable bit manipulation utilities for
 * embedded applications.
 *
 * Supported Type:
 *      uint32_t
 *
 * Thread Safety:
 *      Reentrant
 *
 * Dynamic Memory:
 *      None
 ******************************************************************************/

#ifndef BITUTILS_H

#define BITUTILS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define BIT_UTILS_WORD_SIZE_BITS (32U)

#define BIT_UTILS_MAX_BIT_POSITION (31U)

typedef enum
{
    BITUTILS_SUCCESS = 0,
    BITUTILS_ERROR_NULL_POINTER,
    BITUTILS_ERROR_INVALID_BIT_POSITION,
    BITUTILS_ERROR_INVALID_START_BIT,
    BITUTILS_ERROR_INVALID_NUM_BITS,
    BITUTILS_ERROR_INVALID_VALUE
} BitUtilsStatus_t;


/**
 * @brief Set a specific bit in a 32-bit value
 *
 * @param value The input 32-bit value
 * @param bitPosition The position of the bit to set
 * @param updatedValue Pointer to store the updated value
 * @return BitUtilsStatus_t The status of the operation
 */
BitUtilsStatus_t bitUtilsSetBit(uint32_t value, uint8_t bitPosition, uint32_t *updatedValue);

/**
 * @brief Clear a specific bit in a 32-bit value
 *
 * @param value The input 32-bit value
 * @param bitPosition The position of the bit to clear
 * @param updatedValue Pointer to store the updated value
 * @return BitUtilsStatus_t The status of the operation
 */
BitUtilsStatus_t bitUtilsClearBit(uint32_t value, uint8_t bitPosition, uint32_t *updatedValue);

/**
 * @brief Toggle a specific bit in a 32-bit value
 *
 * @param value The input 32-bit value
 * @param bitPosition The position of the bit to toggle
 * @param updatedValue Pointer to store the updated value
 * @return BitUtilsStatus_t The status of the operation
 */
BitUtilsStatus_t bitUtilsToggleBit(uint32_t value, uint8_t bitPosition, uint32_t *updatedValue);

/**
 * @brief Check if a specific bit is set in a 32-bit value
 *
 * @param value The input 32-bit value
 * @param bitPosition The position of the bit to check
 * @return BitUtilsStatus_t The status of the operation
 */
BitUtilsStatus_t bitUtilsIsBitSet(uint32_t value, uint8_t bitPosition, bool *isSet);

/**
 * @brief Extract bits from a 32-bit value
 *
 * @param value The input 32-bit value
 * @param startBit The starting position of the bits to extract
 * @param numBits The number of bits to extract
 * @param extractedBits Pointer to store the extracted bits
 * @return BitUtilsStatus_t The status of the operation
 */
BitUtilsStatus_t bitUtilsExtractBits(uint32_t value, uint8_t startBit, uint8_t numBits, uint32_t *extractedBits);

/**
 * @brief Insert bits into a 32-bit value
 *
 * @param value The input 32-bit value
 * @param bitsToInsert The bits to insert into the value
 * @param startBit The starting position to insert the bits
 * @param numBits The number of bits to insert
 * @param updatedValue Pointer to store the updated value
 * @return BitUtilsStatus_t The status of the operation
 */
BitUtilsStatus_t bitUtilsInsertBits(uint32_t value, uint32_t bitsToInsert, uint8_t startBit, uint8_t numBits, uint32_t *updatedValue);

#endif

/**************************************** END OF bitUtils.h ****************************************/