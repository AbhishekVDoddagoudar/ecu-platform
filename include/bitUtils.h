/******************************************************************************
 * @file    bitUtils.h
 * @brief   Public interface for bit manipulation utilities.
 *
 * @author  Abhishek Doddagoudar
 *
 * @date    July 2026
 ******************************************************************************/

#pragma once

#include <stdint.h>
#include <stdbool.h>

#define BITUTILS_WORD_SIZE 32U

/**
 * @brief Set a specific bit in a 32-bit value
 *
 * @param value The input 32-bit value
 * @param bitPosition The position of the bit to set
 * @return uint32_t The updated value
 */
uint32_t bitUtilsSetBit(uint32_t value, uint8_t bitPosition);

/**
 * @brief Clear a specific bit in a 32-bit value
 *
 * @param value The input 32-bit value
 * @param bitPosition The position of the bit to clear
 * @return uint32_t The updated value
 */
uint32_t bitUtilsClearBit(uint32_t value, uint8_t bitPosition);

/**
 * @brief Toggle a specific bit in a 32-bit value
 *
 * @param value The input 32-bit value
 * @param bitPosition The position of the bit to toggle
 * @return uint32_t The updated value
 */
uint32_t bitUtilsToggleBit(uint32_t value, uint8_t bitPosition);

/**
 * @brief Check if a specific bit is set in a 32-bit value
 *
 * @param value The input 32-bit value
 * @param bitPosition The position of the bit to check
 * @return bool True if the bit is set, false otherwise
 */
bool bitUtilsIsBitSet(uint32_t value, uint8_t bitPosition);

/**
 * @brief Extract bits from a 32-bit value
 *
 * @param value The input 32-bit value
 * @param startBit The starting position of the bits to extract
 * @param numBits The number of bits to extract
 * @return uint32_t The extracted bits
 */
uint32_t bitUtilsExtractBits(uint32_t value, uint8_t startBit, uint8_t numBits);

/**
 * @brief Insert bits into a 32-bit value
 *
 * @param value The input 32-bit value
 * @param bitsToInsert The bits to insert into the value
 * @param startBit The starting position to insert the bits
 * @param numBits The number of bits to insert
 * @return uint32_t The updated value
 */
uint32_t bitUtilsInsertBits(uint32_t value, uint32_t bitsToInsert, uint8_t startBit, uint8_t numBits);

/**************************************** END OF bitUtils.h ****************************************/