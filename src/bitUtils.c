/******************************************************************************
 * @file   bitUtils.c
 * @brief  Implementation of bit manipulation utilities.
 *
 * @author Abhishek Doddagoudar
 *
 * @date   July 2026
 ******************************************************************************/

#include <bitUtils.h>
#include <assert.h>

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
 * @return uint32_t The updated 32-bit value
 */
uint32_t bitUtilsSetBit(uint32_t value, uint8_t bitPosition)
{
    assert(bitPosition < 32);

    return value | (1U << bitPosition);
}

/**
 * @brief Clear a specific bit in a 32-bit value
 *
 * @details This function clears a specific bit in a 32-bit unsigned integer to 0.
 *          It takes the input value and the position of the bit to be cleared, and returns the updated value with the specified bit cleared.
 *
 * @param value The input 32-bit value
 * @param bitPosition The position of the bit to clear
 *
 * @exception None
 *
 * @return uint32_t The updated 32-bit value
 */
uint32_t bitUtilsClearBit(uint32_t value, uint8_t bitPosition)
{
    assert(bitPosition < 32);

    return value & ~(1U << bitPosition);
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
 * @return uint32_t The updated 32-bit value
 */
uint32_t bitUtilsToggleBit(uint32_t value, uint8_t bitPosition)
{
    assert(bitPosition < 32);

    return value ^ (1U << bitPosition);
}

/**
 * @brief Check if a specific bit is set in a 32-bit value
 *
 * @details This function checks if a specific bit in a 32-bit unsigned integer is set (1).
 *
 * @param value The input 32-bit value
 * @param bitPosition The position of the bit to check
 *
 * @exception None
 *
 * @return bool True if the bit is set, false otherwise
 */
bool bitUtilsIsBitSet(uint32_t value, uint8_t bitPosition)
{
    assert(bitPosition < BITUTILS_WORD_SIZE);

    return (value & (1U << bitPosition)) != 0;
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
 * @return uint32_t The extracted bits
 */
uint32_t bitUtilsExtractBits(uint32_t value, uint8_t startBit, uint8_t numBits)
{
    assert(startBit < BITUTILS_WORD_SIZE);
    assert(numBits > 0 && numBits <= BITUTILS_WORD_SIZE - startBit);

    uint32_t mask = 0;

    if (numBits == BITUTILS_WORD_SIZE)
    {
        return value;
    }
    else
    {
        mask = ((1U << numBits) - 1) << startBit;
        return (value & mask) >> startBit;
    }
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
 * @return uint32_t The updated value
 */
uint32_t bitUtilsInsertBits(uint32_t value, uint32_t bitsToInsert, uint8_t startBit, uint8_t numBits)
{
    assert(startBit < BITUTILS_WORD_SIZE);
    assert(numBits > 0 && numBits <= BITUTILS_WORD_SIZE - startBit);

    uint32_t mask = 0;

    if (numBits == BITUTILS_WORD_SIZE)
    {
        mask = 0xFFFFFFFF;
    }
    else
    {
        mask = ((1U << numBits) - 1) << startBit;
    }

    value &= ~mask;                             // Clear the bits at the specified position
    value |= (bitsToInsert << startBit) & mask; // Insert the new bits

    return value;
}

/*****************************************End of bitUtils.c*****************************************/