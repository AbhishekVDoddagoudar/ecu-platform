/******************************************************************************
 * @file   crc16.c
 * @brief  Implementation of CRC-16 checksum calculation.
 *
 * @author Abhishek Doddagoudar
 *
 * @date   July 2026
 ******************************************************************************/

#include <crc16.h>

/**
 * @brief Calculate CRC-16/CCITT-FALSE checksum
 *
 * @details This function calculates the CRC-16/CCITT-FALSE checksum for a
 *          given data buffer, using polynomial 0x1021 with an initial value
 *          of 0xFFFF, no input/output reflection.
 *
 * @note Implements the CRC16-CCITT-FALSE variant.
 *
 * @param data Pointer to the input data buffer for which the CRC-16 checksum is to be calculated.
 * @param length Length of the input data buffer in bytes.
 *
 * @exception None
 *
 * @return uint16_t
 */
uint16_t crc16Calculate(const uint8_t *data, size_t length)
{
    uint16_t crc = CRC16_INITIAL_VALUE;

    if (data == NULL)
    {
        return CRC16_INITIAL_VALUE;
    }

    for (size_t i = 0; i < length; i++)
    {
        crc ^= (uint16_t)data[i] << 8;
        for (int bit = 0; bit < 8; bit++)
        {
            if (crc & 0x8000)
            {
                crc = (crc << 1) ^ CRC16_POLYNOMIAL;
            }
            else
            {
                crc <<= 1;
            }
        }
    }

    return crc;
}

/**
 * @brief Update CRC-16/CCITT-FALSE checksum incrementally
 *
 * @details This function updates a CRC-16/CCITT-FALSE checksum for a given
 *          data buffer, using polynomial 0x1021, allowing the CRC to be
 *          computed incrementally across multiple calls (e.g. as data
 *          arrives in chunks). Passing 0xFFFF as currentCRC starts a new
 *          calculation; passing a running total continues it.
 *
 * @note Implements the CRC16-CCITT-FALSE variant (same algorithm as
 *       crc16Calculate). Must stay in sync with crc16Calculate.
 *
 * @param currentCRC The current CRC-16 value (0xFFFF to start a new CRC).
 * @param data Pointer to the input data buffer to fold into the checksum.
 * @param length Length of the input data buffer in bytes.
 *
 * @exception None
 *
 * @return uint16_t The updated CRC-16 checksum.
 */
uint16_t crc16Update(uint16_t currentCRC, const uint8_t *data, size_t length)
{
    uint16_t crc = currentCRC;

    if (data == NULL || length == 0)
    {
        return crc;
    }

    for (size_t i = 0; i < length; i++)
    {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t bit = 0; bit < 8; bit++)
        {
            if (crc & 0x8000)
            {
                crc = (crc << 1) ^ CRC16_POLYNOMIAL;
            }
            else
            {
                crc <<= 1;
            }
        }
    }

    return crc;
}

/**************************************** END OF crc16.c ****************************************/