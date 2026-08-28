/******************************************************************************
 * @file   crc32.h
 * @brief  Public interface for CRC-32/ISO-HDLC calculation.
 *
 * @author Abhishek Doddagoudar
 *
 * @date   August 2026
 *
 * Provides reusable CRC-32 checksum utilities for embedded applications.
 *
 * Supported Variant:
 *      CRC-32/ISO-HDLC (also known plainly as "CRC-32" -- Ethernet, ZIP,
 *      PNG, gzip)
 *
 * Parameters:
 *      Polynomial : 0x04C11DB7 (normal form)
 *      Init Value : 0xFFFFFFFF
 *      RefIn      : Enabled
 *      RefOut     : Enabled
 *      Final XOR  : 0xFFFFFFFF
 *      Check      : 0xCBF43926 for ASCII input "123456789"
 *
 * Canonical Test Vector:
 *      Input : "123456789" (9 bytes, ASCII, no null terminator)
 *      CRC   : 0xCBF43926
 *      This is the standard reference check value for CRC-32/ISO-HDLC.
 *      Any correct implementation of this module must reproduce it; the
 *      unit tests assert this directly.
 *
 * This is a standalone module, not a generalization of crc16 -- see
 * docs/crc32-requirements.md for why. crc16 supports exactly one
 * non-reflected variant and has no reflection machinery to extend;
 * building a genuinely generic CRC engine from a sample size of one
 * additional variant was judged premature. Internally this module uses a
 * reflected, bit-by-bit, right-shift algorithm -- see
 * docs/crc32-design.md for the full derivation and reasoning; not
 * repeated here since this header documents the API, not the
 * implementation.
 *
 * Features:
 *      - One-shot CRC calculation
 *      - Incremental CRC calculation
 *      - No dynamic memory allocation
 *      - Reentrant design
 *
 * Thread Safety:
 *      Reentrant. All calculation state lives in a caller-owned
 *      CRC32_Context_t. Multiple callers may run independent CRC
 *      calculations concurrently using separate contexts. This module
 *      holds no global or static state. If two callers share the same
 *      context, synchronizing access to it is the caller's responsibility.
 *
 * Dynamic Memory:
 *      None
 *
 * Example Usage (incremental):
 *
 *      CRC32_Context_t ctx;
 *      uint32_t crc;
 *
 *      crc32Init(&ctx);
 *      crc32Update(&ctx, buffer1, len1);
 *      crc32Update(&ctx, buffer2, len2);
 *      crc32GetResult(&ctx, &crc);
 *
 * Example Usage (one-shot):
 *
 *      uint32_t crc;
 *      crc32Compute(buffer, len, &crc);
 *
 ******************************************************************************/

#ifndef CRC32_H
#define CRC32_H

#include <stdint.h>
#include <stddef.h>

/** CRC-32/ISO-HDLC polynomial, normal representation. See crc32.c for
 *  the bit-reversed form actually used internally, and
 *  docs/crc32-design.md for why those are the same polynomial, not two
 *  different ones. */
#define CRC32_POLYNOMIAL (0x04C11DB7U)

/** CRC-32/ISO-HDLC initial value. */
#define CRC32_INITIAL_VALUE (0xFFFFFFFFU)

/** CRC-32/ISO-HDLC final XOR value. */
#define CRC32_FINAL_XOR (0xFFFFFFFFU)

/**
 * @brief CRC calculation context.
 *
 * Stores the current state of an ongoing CRC calculation.
 *
 * The context must be owned by the caller to maintain reentrancy.
 *
 * @note Holds the raw, pre-final-XOR running accumulator between calls --
 *       the final XOR is applied only inside crc32GetResult(), never
 *       written back into the context. This is required for correct
 *       incremental operation (see docs/crc32-design.md, Section 6);
 *       applying the final XOR inside crc32Update() would silently
 *       corrupt the result of any multi-call incremental sequence.
 *
 * @note No "initialized" flag is carried here, unlike some other modules
 *       in this project (e.g. the memory pool). An uninitialized context
 *       produces a wrong CRC value, not an out-of-bounds access -- see
 *       docs/crc32-design.md, Section 3, for the full reasoning. Calling
 *       crc32Update()/crc32GetResult() before crc32Init() is a caller
 *       error that is not detected by this module.
 *
 * @note Future extensions may add additional members. Do not assume the
 *       struct's size or layout is stable across versions.
 */
typedef struct
{
    uint32_t currentCRC;
} CRC32_Context_t;

typedef enum
{
    CRC32_SUCCESS = 0,
    CRC32_ERROR_NULL_POINTER,
    CRC32_ERROR_MISMATCH
} CRC32_Status_t;

/**
 * @brief Initialize CRC context.
 *
 * Initializes CRC calculation with the configured initial value.
 *
 * @param ctx Pointer to CRC context.
 *
 * @return
 *      CRC32_SUCCESS            -> ctx initialized
 *      CRC32_ERROR_NULL_POINTER -> ctx == NULL; no operation performed
 */
CRC32_Status_t crc32Init(CRC32_Context_t *ctx);

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
 *       zero bytes leaves the context's CRC state unchanged. This holds
 *       even when data == NULL -- a NULL/zero-length pair is a valid
 *       no-op, not an error; NULL is only rejected when len > 0 (see
 *       table below).
 *
 * @param ctx  CRC context.
 * @param data Input buffer.
 * @param len  Number of bytes.
 *
 * @return
 *      ctx      data     len     Result
 *      NULL     any      any     CRC32_ERROR_NULL_POINTER
 *      non-NULL NULL     0       CRC32_SUCCESS (no-op)
 *      non-NULL NULL     >0      CRC32_ERROR_NULL_POINTER
 *      non-NULL non-NULL any     CRC32_SUCCESS
 */
CRC32_Status_t crc32Update(CRC32_Context_t *ctx, const uint8_t *data, size_t len);

/**
 * @brief Get current CRC value.
 *
 * Applies the final XOR (0xFFFFFFFF) to the context's running accumulator
 * and returns the result. Does not invalidate the context, and does not
 * modify ctx->currentCRC -- additional Update() calls are allowed after
 * this call and continue correctly from the pre-XOR accumulator (see
 * docs/crc32-design.md, Section 6).
 *
 * @param ctx CRC context.
 * @param crc Calculated CRC value.
 *
 * @return
 *      CRC32_SUCCESS            -> *crc set to the final CRC value
 *      CRC32_ERROR_NULL_POINTER -> ctx == NULL or crc == NULL; *crc left
 *                                   unmodified
 */
CRC32_Status_t crc32GetResult(const CRC32_Context_t *ctx, uint32_t *crc);

/**
 * @brief Calculate CRC for complete buffer.
 *
 * Convenience API. Internally a thin wrapper that calls, in order:
 *
 *      crc32Init()
 *      crc32Update()
 *      crc32GetResult()
 *
 * No CRC logic is duplicated between the streaming and one-shot APIs.
 *
 * @note Same NULL/len contract as crc32Update() -- data == NULL is only
 *       an error when len > 0.
 *
 * @param data Input buffer.
 * @param len  Number of bytes.
 * @param crc  Calculated CRC value.
 *
 * @return
 *      CRC32_SUCCESS            -> *crc set to the final CRC value
 *      CRC32_ERROR_NULL_POINTER -> crc == NULL, or data == NULL with
 *                                   len > 0
 */
CRC32_Status_t crc32Compute(const uint8_t *data, size_t len, uint32_t *crc);

/**
 * @brief Verify received CRC against calculated CRC.
 *
 * @param data        Input buffer.
 * @param len         Number of bytes.
 * @param receivedCRC CRC received from communication interface.
 *
 * @note Same NULL/len contract as crc32Update()/crc32Compute() -- data ==
 *       NULL is only an error when len > 0.
 *
 * @return
 *      CRC32_SUCCESS             -> CRC matches
 *      CRC32_ERROR_MISMATCH      -> calculated CRC does not match receivedCRC
 *      CRC32_ERROR_NULL_POINTER  -> data == NULL with len > 0; comparison
 *                                    never attempted
 */
CRC32_Status_t crc32Verify(const uint8_t *data, size_t len, uint32_t receivedCRC);

#endif /* CRC32_H */

/******************************** END OF crc32.h ********************************/