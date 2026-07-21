/******************************************************************************
 * @file   bitUtils.c
 * @brief  Implementation of bit manipulation utilities.
 *
 * @author Abhishek Doddagoudar
 *
 * @date   July 2026
 *
 * Supports:
 * - uint32_t
 *
 *  Thread Safety
 * - Reentrant
 *
 * Dynamic Allocation:
 * - None
 *
 * Dependencies:
 * - stdint.h
 ******************************************************************************/

#include <bitUtils.h>

/**
 * @internal
 * @brief Validates a single bit position against the word width.
 *
 * @param bitPosition Bit position to validate.
 *
 * @return BitUtilsStatus_t
 * @retval BITUTILS_SUCCESS Bit position is within range.
 * @retval BITUTILS_ERROR_INVALID_BIT_POSITION Bit position is >= BIT_UTILS_WORD_SIZE_BITS.
 */
static BitUtilsStatus_t validateBitPosition(uint8_t bitPosition)
{
    if (bitPosition >= BIT_UTILS_WORD_SIZE_BITS)
    {
        return BITUTILS_ERROR_INVALID_BIT_POSITION;
    }
    return BITUTILS_SUCCESS;
}

/**
 * @internal
 * @brief Validates a start bit and bit-count pair against the word width.
 *
 * @param startBit Starting bit position.
 * @param numBits  Number of bits spanning from startBit.
 *
 * @return BitUtilsStatus_t
 * @retval BITUTILS_SUCCESS Range is valid.
 * @retval BITUTILS_ERROR_INVALID_NUM_BITS numBits is 0 or exceeds the remaining bits from startBit.
 */
static BitUtilsStatus_t validateBitRange(uint8_t startBit, uint8_t numBits)
{
    if (startBit >= BIT_UTILS_WORD_SIZE_BITS)
    {
        return BITUTILS_ERROR_INVALID_START_BIT;
    }
    if ((numBits == 0U) || (numBits > (BIT_UTILS_WORD_SIZE_BITS - startBit)))
    {
        return BITUTILS_ERROR_INVALID_NUM_BITS;
    }
    return BITUTILS_SUCCESS;
}

/**
 * @internal
 * @brief Generates a right-aligned bitmask of the given width.
 *
 * @param numBits Width of the mask in bits (0 to BIT_UTILS_WORD_SIZE_BITS).
 *
 * @return uint32_t Mask with the low numBits bits set.
 *
 * @note Precondition: numBits <= BIT_UTILS_WORD_SIZE_BITS, enforced by
 *       validateBitRange() at every call site.
 */
static uint32_t generateMask(uint8_t numBits)
{
    return (numBits == BIT_UTILS_WORD_SIZE_BITS) ? UINT32_MAX : ((1U << numBits) - 1U);
}

/**
 * @brief Set a specific bit in a 32-bit value
 *
 * @details This function sets a specific bit in a 32-bit unsigned integer to 1.
 *          It takes the input value and the position of the bit to be set, and returns the updated value with the specified bit set.
 *
 * @param value The input 32-bit value
 * @param bitPosition The position of the bit to set
 *
 * @exception None
 *
 * @return BitUtilsStatus_t The status of the operation
 *
 * @retval BITUTILS_SUCCESS The bit was successfully set
 * @retval BITUTILS_ERROR_INVALID_BIT_POSITION The specified bit position is invalid (greater than 31)
 * @retval BITUTILS_ERROR_NULL_POINTER The pointer to store the updated value is NULL
 */
BitUtilsStatus_t bitUtilsSetBit(uint32_t value, uint8_t bitPosition, uint32_t *updatedValue)
{
    BitUtilsStatus_t status;

    if (updatedValue == NULL)
    {
        return BITUTILS_ERROR_NULL_POINTER;
    }

    status = validateBitPosition(bitPosition);

    if (status != BITUTILS_SUCCESS)
    {
        return status;
    }

    *updatedValue = value | (1U << bitPosition);

    return BITUTILS_SUCCESS;
}

/**
 * @brief Clear a specific bit in a 32-bit value
 *
 * @details This function clears a specific bit in a 32-bit unsigned integer to 0.
 *          It takes the input value and the position of the bit to be cleared, and returns the updated value with the specified bit cleared.
 *
 * @param value The input 32-bit value
 * @param bitPosition The position of the bit to clear
 * @param updatedValue Pointer to store the updated value
 *
 * @exception None
 *
 * @return BitUtilsStatus_t The status of the operation
 *
 * @retval BITUTILS_SUCCESS The bit was successfully cleared
 * @retval BITUTILS_ERROR_INVALID_BIT_POSITION The specified bit position is invalid (greater than 31)
 * @retval BITUTILS_ERROR_NULL_POINTER The pointer to store the updated value is NULL
 */
BitUtilsStatus_t bitUtilsClearBit(uint32_t value, uint8_t bitPosition, uint32_t *updatedValue)
{
    BitUtilsStatus_t status;

    if (updatedValue == NULL)
    {
        return BITUTILS_ERROR_NULL_POINTER;
    }

    status = validateBitPosition(bitPosition);

    if (status != BITUTILS_SUCCESS)
    {
        return status;
    }

    *updatedValue = value & ~(1U << bitPosition);

    return BITUTILS_SUCCESS;
}

/**
 * @brief Toggle a specific bit in a 32-bit value
 *
 * @details This function toggles a specific bit in a 32-bit unsigned integer.
 *          It takes the input value and the position of the bit to be toggled, and returns the updated value with the specified bit toggled.
 *
 * @param value The input 32-bit value
 * @param bitPosition The position of the bit to toggle
 *
 * @exception None
 *
 * @return BitUtilsStatus_t The status of the operation
 *
 * @retval BITUTILS_SUCCESS The bit was successfully toggled
 * @retval BITUTILS_ERROR_INVALID_BIT_POSITION The specified bit position is invalid (greater than 31)
 * @retval BITUTILS_ERROR_NULL_POINTER The pointer to store the updated value is NULL
 */
BitUtilsStatus_t bitUtilsToggleBit(uint32_t value, uint8_t bitPosition, uint32_t *updatedValue)
{
    BitUtilsStatus_t status;

    if (updatedValue == NULL)
    {
        return BITUTILS_ERROR_NULL_POINTER;
    }

    status = validateBitPosition(bitPosition);

    if (status != BITUTILS_SUCCESS)
    {
        return status;
    }

    *updatedValue = value ^ (1U << bitPosition);

    return BITUTILS_SUCCESS;
}

/**
 * @brief Check if a specific bit is set in a 32-bit value
 *
 * @details This function checks if a specific bit in a 32-bit unsigned integer is set (1).
 *
 * @param value The input 32-bit value
 * @param bitPosition The position of the bit to check
 * @param isSet Pointer to store the result
 *
 * @exception None
 *
 * @return BitUtilsStatus_t The status of the operation
 * @retval BITUTILS_SUCCESS The bit was successfully checked
 * @retval BITUTILS_ERROR_INVALID_BIT_POSITION The specified bit position is invalid (greater than 31)
 * @retval BITUTILS_ERROR_NULL_POINTER The pointer to store the result is NULL
 */
BitUtilsStatus_t bitUtilsIsBitSet(uint32_t value, uint8_t bitPosition, bool *isSet)
{
    BitUtilsStatus_t status;

    if (isSet == NULL)
    {
        return BITUTILS_ERROR_NULL_POINTER;
    }

    status = validateBitPosition(bitPosition);

    if (status != BITUTILS_SUCCESS)
    {
        return status;
    }

    *isSet = (value & (1U << bitPosition)) != 0;

    return BITUTILS_SUCCESS;
}

/**
 * @brief Extract bits from a 32-bit value
 *
 * @details This function extracts a specified number of bits from a 32-bit unsigned integer.
 *
 * @param value The input 32-bit value
 * @param startBit The starting position of the bits to extract
 * @param numBits The number of bits to extract
 *
 * @exception None
 *
 * @return BitUtilsStatus_t The status of the operation
 *
 * @retval BITUTILS_SUCCESS The bits were successfully extracted
 * @retval BITUTILS_ERROR_INVALID_START_BIT The specified start bit position is invalid (greater than 31)
 * @retval BITUTILS_ERROR_INVALID_NUM_BITS The specified number of bits to extract is invalid (0 or greater than the remaining bits from startBit)
 * @retval BITUTILS_ERROR_INVALID_VALUE The extracted value is invalid (e.g., if the extracted bits exceed the maximum value for the specified number of bits)
 * @retval BITUTILS_ERROR_NULL_POINTER The pointer to store the extracted bits is NULL
 */
BitUtilsStatus_t bitUtilsExtractBits(uint32_t value, uint8_t startBit, uint8_t numBits, uint32_t *extractedBits)
{
    BitUtilsStatus_t status;
    uint32_t mask = 0;

    if (extractedBits == NULL)
    {
        return BITUTILS_ERROR_NULL_POINTER;
    }

    status = validateBitRange(startBit, numBits);

    if (status != BITUTILS_SUCCESS)
    {
        return status;
    }

    mask = generateMask(numBits) << startBit;
    *extractedBits = (value & mask) >> startBit;

    return BITUTILS_SUCCESS;
}

/**
 * @brief Insert bits into a 32-bit value
 *
 * @details This function inserts a specified number of bits into a 32-bit unsigned integer.
 *
 * @param value The input 32-bit value
 * @param bitsToInsert The bits to insert
 * @param startBit The starting position of the bits to insert
 * @param numBits The number of bits to insert
 *
 * @exception None
 *
 * @return BitUtilsStatus_t The status of the operation
 *
 * @retval BITUTILS_SUCCESS The bits were successfully inserted
 * @retval BITUTILS_ERROR_INVALID_START_BIT The specified start bit position is invalid (greater than 31)
 * @retval BITUTILS_ERROR_INVALID_NUM_BITS The specified number of bits to insert is invalid (0 or greater than the remaining bits from startBit)
 * @retval BITUTILS_ERROR_INVALID_VALUE The bits to insert are invalid (e.g., if the bits exceed the maximum value for the specified number of bits)
 * @retval BITUTILS_ERROR_NULL_POINTER The pointer to store the updated value is NULL
 */
BitUtilsStatus_t bitUtilsInsertBits(uint32_t value, uint32_t bitsToInsert, uint8_t startBit, uint8_t numBits, uint32_t *updatedValue)
{
    BitUtilsStatus_t status;
    uint32_t maxValueForWidth = 0;
    uint32_t mask = 0;

    if (updatedValue == NULL)
    {
        return BITUTILS_ERROR_NULL_POINTER;
    }

    status = validateBitRange(startBit, numBits);
    
    if (status != BITUTILS_SUCCESS)
    {
        return status;
    }

    maxValueForWidth = generateMask(numBits);
    if (bitsToInsert > maxValueForWidth)
    {
        return BITUTILS_ERROR_INVALID_VALUE;
    }

    mask = maxValueForWidth << startBit;

    value &= ~mask;                             // Clear the bits at the specified position
    value |= (bitsToInsert << startBit) & mask; // Insert the new bits

    *updatedValue = value;
    return BITUTILS_SUCCESS;
}

/*****************************************End of bitUtils.c*****************************************/