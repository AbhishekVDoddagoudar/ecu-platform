/******************************************************************************
 * @file   crc16.h
 * @brief  Public interface for CRC-16 checksum calculation.
 *
 * @author Abhishek Doddagoudar
 *
 * @date   July 2026
 ******************************************************************************/
#pragma once

#include <stdint.h>
#include <stddef.h>

#define CRC16_POLYNOMIAL 0x1021U

#define CRC16_INITIAL_VALUE 0xFFFFU

/**
 * @brief Calculates the CRC16-CCITT checksum of a buffer.
 *
 * @param data Pointer to the input buffer.
 * @param length Number of bytes in the buffer.
 *
 * @return Calculated CRC16 value.
 *
 * @note Implements the CRC16-CCITT-FALSE variant.
 */
uint16_t crc16Calculate(const uint8_t *data, size_t length);

/**
 * @brief Updates the CRC16-CCITT checksum with additional data.
 *
 * @param currentCRC The current CRC value to be updated.
 * @param data Pointer to the input buffer.
 * @param length Number of bytes in the buffer.
 *
 * @return Updated CRC16 value.
 *
 * @note Implements the CRC16-CCITT-FALSE variant, allowing incremental updates.
 */
uint16_t crc16Update(uint16_t currentCRC, const uint8_t *data, size_t length);
/**************************************** END OF crc16.h *******************************************/