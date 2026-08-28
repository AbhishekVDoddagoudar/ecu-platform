/******************************************************************************
 * @file   crc32.c
 * @brief  Implementation of CRC-32/ISO-HDLC calculation.
 *
 * @author Abhishek Doddagoudar
 *
 * @date   August 2026
 *
 * Supports:
 * - CRC-32/ISO-HDLC (poly 0x04C11DB7, init 0xFFFFFFFF, RefIn/RefOut
 *   enabled, final XOR 0xFFFFFFFF)
 *
 * Internally implemented as a reflected, bit-by-bit, right-shift
 * algorithm using CRC32_REFLECTED_POLYNOMIAL (0xEDB88320 -- the
 * bit-reversal of the canonical polynomial 0x04C11DB7, not a different
 * variant). See docs/crc32-design.md, Section 2, for the full derivation
 * of why this is equivalent to explicit input/output bit-reversal against
 * the normal-form polynomial, without ever performing that reversal.
 *
 * Thread Safety:
 * - Reentrant (all state lives in the caller-owned CRC32_Context_t)
 *
 * Dynamic Allocation:
 * - None
 *
 * Dependencies:
 * - stdint.h, stddef.h
 ******************************************************************************/

#include <crc32.h>

/* Bit-reversal of CRC32_POLYNOMIAL (0x04C11DB7). This is the value the
 * reflected, right-shift algorithm below actually shifts against -- not a
 * different or alternate CRC-32 variant. Kept private to this file: the
 * public header documents the API's variant by its canonical polynomial
 * and test vector, not by this implementation detail. See
 * docs/crc32-design.md, Section 2. */
#define CRC32_REFLECTED_POLYNOMIAL (0xEDB88320U)

/**
 * @internal
 * @brief Validates a CRC context pointer.
 *
 * @param ctx Context pointer to validate.
 *
 * @return CRC32_Status_t
 * @retval CRC32_SUCCESS Context pointer is non-NULL.
 * @retval CRC32_ERROR_NULL_POINTER Context pointer is NULL.
 */
static CRC32_Status_t validateContext(const CRC32_Context_t *ctx)
{
    if (ctx == NULL)
    {
        return CRC32_ERROR_NULL_POINTER;
    }
    return CRC32_SUCCESS;
}

/**
 * @internal
 * @brief Validates a data buffer against its length.
 *
 * @param data Buffer pointer to validate.
 * @param len  Number of bytes the caller claims are in the buffer.
 *
 * @return CRC32_Status_t
 * @retval CRC32_SUCCESS Buffer/length pair is usable. This includes
 *         data == NULL with len == 0, treated as an empty, no-op buffer
 *         rather than an error -- there is no such thing as an invalid
 *         length for CRC purposes, only a null pointer with bytes to read.
 * @retval CRC32_ERROR_NULL_POINTER data is NULL while len is non-zero.
 *
 * @note len is checked before data is examined for NULL-ness, matching
 *       the documented contract (crc32.h) that data == NULL, len == 0 is
 *       success -- never collapsed into a blanket "if (data == NULL)"
 *       check ahead of len.
 */
static CRC32_Status_t validateBuffer(const uint8_t *data, size_t len)
{
    if ((data == NULL) && (len > 0U))
    {
        return CRC32_ERROR_NULL_POINTER;
    }
    return CRC32_SUCCESS;
}

/**
 * @internal
 * @brief Feeds a single byte through the reflected CRC-32 state machine.
 *
 * Implements the right-shift, LSB-first algorithm described in
 * docs/crc32-design.md, Section 2 and Section 4. RefIn and RefOut are not
 * separate steps anywhere in this function or elsewhere in this file --
 * they fall out of processing bits in this direction with this
 * (reflected) polynomial. There is no bit-reversal of data.
 *
 * @param currentCRC Current CRC accumulator value (raw, pre-final-XOR).
 * @param byteIn     Byte to fold into the CRC.
 *
 * @return uint32_t Updated CRC accumulator value (still pre-final-XOR).
 */
static uint32_t crc32ProcessByte(uint32_t currentCRC, uint8_t byteIn)
{
    uint32_t crc = currentCRC ^ (uint32_t)byteIn;
    uint8_t bitIndex;

    for (bitIndex = 0U; bitIndex < 8U; bitIndex++)
    {
        if ((crc & 0x00000001U) != 0U)
        {
            crc = (crc >> 1) ^ CRC32_REFLECTED_POLYNOMIAL;
        }
        else
        {
            crc = crc >> 1;
        }
    }

    return crc;
}

/**
 * @brief Initialize CRC context.
 *
 * @param ctx Pointer to CRC context.
 *
 * @return CRC32_Status_t
 * @retval CRC32_SUCCESS Context initialized with the configured initial value.
 * @retval CRC32_ERROR_NULL_POINTER ctx is NULL.
 */
CRC32_Status_t crc32Init(CRC32_Context_t *ctx)
{
    CRC32_Status_t status = validateContext(ctx);

    if (status != CRC32_SUCCESS)
    {
        return status;
    }

    ctx->currentCRC = CRC32_INITIAL_VALUE;

    return CRC32_SUCCESS;
}

/**
 * @brief Update CRC calculation with input data.
 *
 * Can be called multiple times for incremental calculation.
 *
 * @param ctx  CRC context.
 * @param data Input buffer.
 * @param len  Number of bytes.
 *
 * @return CRC32_Status_t
 * @retval CRC32_SUCCESS Buffer processed and ctx updated (len == 0 is a
 *         valid no-op; the context's CRC state is left unchanged, and
 *         this holds even when data == NULL -- see validateBuffer()).
 * @retval CRC32_ERROR_NULL_POINTER ctx is NULL, or data is NULL while len > 0.
 */
CRC32_Status_t crc32Update(CRC32_Context_t *ctx, const uint8_t *data, size_t len)
{
    CRC32_Status_t status = validateContext(ctx);
    size_t i;

    if (status != CRC32_SUCCESS)
    {
        return status;
    }

    status = validateBuffer(data, len);
    if (status != CRC32_SUCCESS)
    {
        return status;
    }

    for (i = 0U; i < len; i++)
    {
        ctx->currentCRC = crc32ProcessByte(ctx->currentCRC, data[i]);
    }

    return CRC32_SUCCESS;
}

/**
 * @brief Get current CRC value.
 *
 * Does not invalidate the context. Additional updates are allowed after this
 * call, and continue correctly from the pre-final-XOR accumulator, because
 * the final XOR is applied here to a copy and never written back into
 * ctx->currentCRC (see docs/crc32-design.md, Section 6).
 *
 * @param ctx CRC context.
 * @param crc Pointer to store the calculated CRC value.
 *
 * @return CRC32_Status_t
 * @retval CRC32_SUCCESS CRC value written to *crc.
 * @retval CRC32_ERROR_NULL_POINTER ctx or crc is NULL.
 */
CRC32_Status_t crc32GetResult(const CRC32_Context_t *ctx, uint32_t *crc)
{
    CRC32_Status_t status = validateContext(ctx);

    if (status != CRC32_SUCCESS)
    {
        return status;
    }

    if (crc == NULL)
    {
        return CRC32_ERROR_NULL_POINTER;
    }

    /* Final XOR applied here, once, to a copy of the accumulator --
     * ctx->currentCRC itself is never modified by this function. */
    *crc = ctx->currentCRC ^ CRC32_FINAL_XOR;

    return CRC32_SUCCESS;
}

/**
 * @brief Calculate CRC for a complete buffer.
 *
 * Thin wrapper around crc32Init(), crc32Update(), crc32GetResult() -- no CRC
 * logic is duplicated between the streaming and one-shot APIs.
 *
 * @param data Input buffer.
 * @param len  Number of bytes.
 * @param crc  Pointer to store the calculated CRC value.
 *
 * @return CRC32_Status_t
 * @retval CRC32_SUCCESS CRC value written to *crc.
 * @retval CRC32_ERROR_NULL_POINTER crc is NULL, or data is NULL while len > 0.
 */
CRC32_Status_t crc32Compute(const uint8_t *data, size_t len, uint32_t *crc)
{
    CRC32_Context_t ctx;
    CRC32_Status_t status;

    if (crc == NULL)
    {
        return CRC32_ERROR_NULL_POINTER;
    }

    status = crc32Init(&ctx);
    if (status != CRC32_SUCCESS)
    {
        return status;
    }

    status = crc32Update(&ctx, data, len);
    if (status != CRC32_SUCCESS)
    {
        return status;
    }

    return crc32GetResult(&ctx, crc);
}

/**
 * @brief Verify received CRC against calculated CRC.
 *
 * @param data        Input buffer.
 * @param len         Number of bytes.
 * @param receivedCRC CRC received from communication interface.
 *
 * @return CRC32_Status_t
 * @retval CRC32_SUCCESS Calculated CRC matches receivedCRC.
 * @retval CRC32_ERROR_MISMATCH Calculated CRC does not match receivedCRC.
 * @retval CRC32_ERROR_NULL_POINTER data is NULL while len > 0; comparison
 *         never attempted.
 */
CRC32_Status_t crc32Verify(const uint8_t *data, size_t len, uint32_t receivedCRC)
{
    uint32_t calculatedCRC = 0U;
    CRC32_Status_t status = crc32Compute(data, len, &calculatedCRC);

    if (status != CRC32_SUCCESS)
    {
        return status;
    }

    if (calculatedCRC != receivedCRC)
    {
        return CRC32_ERROR_MISMATCH;
    }

    return CRC32_SUCCESS;
}

/*****************************************End of crc32.c*****************************************/