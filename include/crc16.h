/******************************************************************************
 * @file   crc16.h
 * @brief  Public interface for CRC-16-CCITT-FALSE calculation.
 *
 * @author Abhishek Doddagoudar
 *
 * @date   July 2026
 *
 * Provides reusable CRC-16 checksum utilities for embedded applications.
 *
 * Supported Variant:
 *      CRC-16-CCITT-FALSE
 *
 * Parameters:
 *      Polynomial : 0x1021
 *      Init Value : 0xFFFF
 *      Reflection : Disabled
 *      Final XOR  : 0x0000
 *
 * Features:
 *      - One-shot CRC calculation
 *      - Incremental CRC calculation
 *      - No dynamic memory allocation
 *      - Reentrant design
 *
 * Thread Safety:
 *      Reentrant. All calculation state lives in a caller-owned
 *      CRC16_Context_t. Multiple callers may run independent CRC
 *      calculations concurrently using separate contexts. This module
 *      holds no global or static state. If two callers share the same
 *      context, synchronizing access to it is the caller's responsibility.
 *
 * Dynamic Memory:
 *      None
 *
 * Example Usage (incremental):
 *
 *      CRC16_Context_t ctx;
 *      uint16_t crc;
 *
 *      crc16Init(&ctx);
 *      crc16Update(&ctx, buffer1, len1);
 *      crc16Update(&ctx, buffer2, len2);
 *      crc16GetResult(&ctx, &crc);
 *
 * Example Usage (one-shot):
 *
 *      uint16_t crc;
 *      crc16Compute(buffer, len, &crc);
 *
 ******************************************************************************/

#ifndef CRC16_H
#define CRC16_H

#include <stdint.h>
#include <stddef.h>

#define CRC16_CCITT_FALSE_POLYNOMIAL (0x1021U)

#define CRC16_INITIAL_VALUE (0xFFFFU)

#define CRC16_TOP_BIT (0x8000U)

/**
 * @brief CRC calculation context.
 *
 * Stores the current state of an ongoing CRC calculation.
 *
 * The context must be owned by the caller to maintain reentrancy.
 *
 * @note Future extensions may add additional members (e.g. processed byte
 *       count, configurable polynomial). Do not assume the struct's size
 *       or layout is stable across versions.
 */
typedef struct
{
    uint16_t currentCRC;
} CRC16_Context_t;

typedef enum
{
    CRC16_SUCCESS = 0,
    CRC16_ERROR_NULL_POINTER,
    CRC16_ERROR_MISMATCH
} CRC16_Status_t;

/**
 * @brief Initialize CRC context.
 *
 * Initializes CRC calculation with the configured initial value.
 *
 * @param ctx Pointer to CRC context.
 *
 * @return CRC16_Status_t
 */
CRC16_Status_t crc16Init(CRC16_Context_t *ctx);

/**
 * @brief Update CRC calculation with input data.
 *
 * Can be called multiple times for incremental calculation.
 *
 * Example:
 *
 *      Init()
 *
 *      Update(data1)
 *
 *      Update(data2)
 *
 *      GetResult()
 *
 * @note len == 0 is a valid, well-defined no-op (not an error). Processing
 *       zero bytes leaves the context's CRC state unchanged.
 *
 * @param ctx  CRC context.
 * @param data Input buffer.
 * @param len  Number of bytes.
 *
 * @return CRC16_Status_t
 */
CRC16_Status_t crc16Update(CRC16_Context_t *ctx, const uint8_t *data, size_t len);

/**
 * @brief Get current CRC value.
 *
 * Does not invalidate the context.
 * Additional updates are allowed after this call.
 *
 * @param ctx CRC context.
 * @param crc Calculated CRC value.
 *
 * @return CRC16_Status_t
 */
CRC16_Status_t crc16GetResult(const CRC16_Context_t *ctx, uint16_t *crc);

/**
 * @brief Calculate CRC for complete buffer.
 *
 * Convenience API. Internally a thin wrapper that calls, in order:
 *
 *      crc16Init()
 *      crc16Update()
 *      crc16GetResult()
 *
 * No CRC logic is duplicated between the streaming and one-shot APIs.
 *
 * @param data Input buffer.
 * @param len  Number of bytes.
 * @param crc  Calculated CRC value.
 *
 * @return CRC16_Status_t
 */
CRC16_Status_t crc16Compute(const uint8_t *data, size_t len, uint16_t *crc);

/**
 * @brief Verify received CRC against calculated CRC.
 *
 * @param data        Input buffer.
 * @param len         Number of bytes.
 * @param receivedCRC CRC received from communication interface.
 *
 * @return
 *      CRC16_SUCCESS         -> CRC matches
 *      CRC16_ERROR_MISMATCH  -> CRC mismatch
 *      Other errors          -> Parameter failure
 */
CRC16_Status_t crc16Verify(const uint8_t *data, size_t len, uint16_t receivedCRC);

#endif /* CRC16_H */

/******************************** END OF crc16.h ********************************/