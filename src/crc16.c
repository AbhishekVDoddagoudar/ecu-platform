/******************************************************************************
 * @file   crc16.c
 * @brief  Implementation of CRC-16-CCITT-FALSE calculation.
 *
 * @author Abhishek Doddagoudar
 *
 * @date   July 2026
 *
 * Supports:
 * - CRC-16-CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection, no final XOR)
 *
 * Thread Safety:
 * - Reentrant (all state lives in the caller-owned CRC16_Context_t)
 *
 * Dynamic Allocation:
 * - None
 *
 * Dependencies:
 * - stdint.h, stddef.h
 ******************************************************************************/

#include <crc16.h>

/**
 * @internal
 * @brief Validates a CRC context pointer.
 *
 * @param ctx Context pointer to validate.
 *
 * @return CRC16_Status_t
 * @retval CRC16_SUCCESS Context pointer is non-NULL.
 * @retval CRC16_ERROR_NULL_POINTER Context pointer is NULL.
 */
static CRC16_Status_t validateContext(const CRC16_Context_t *ctx)
{
    if (ctx == NULL)
    {
        return CRC16_ERROR_NULL_POINTER;
    }
    return CRC16_SUCCESS;
}

/**
 * @internal
 * @brief Validates a data buffer against its length.
 *
 * @param data Buffer pointer to validate.
 * @param len  Number of bytes the caller claims are in the buffer.
 *
 * @return CRC16_Status_t
 * @retval CRC16_SUCCESS Buffer/length pair is usable. This includes
 *         data == NULL with len == 0, treated as an empty, no-op buffer
 *         rather than an error -- there is no such thing as an invalid
 *         length for CRC purposes, only a null pointer with bytes to read.
 * @retval CRC16_ERROR_NULL_POINTER data is NULL while len is non-zero.
 */
static CRC16_Status_t validateBuffer(const uint8_t *data, size_t len)
{
    if ((data == NULL) && (len > 0U))
    {
        return CRC16_ERROR_NULL_POINTER;
    }
    return CRC16_SUCCESS;
}

/**
 * @internal
 * @brief Feeds a single byte through the CRC-16-CCITT-FALSE state machine.
 *
 * @param currentCRC Current CRC accumulator value.
 * @param byteIn     Byte to fold into the CRC.
 *
 * @return uint16_t Updated CRC accumulator value.
 */
static uint16_t crc16ProcessByte(uint16_t currentCRC, uint8_t byteIn)
{
    uint16_t crc = currentCRC ^ ((uint16_t)byteIn << 8);
    uint8_t bitIndex;

    for (bitIndex = 0U; bitIndex < 8U; bitIndex++)
    {
        if ((crc & CRC16_TOP_BIT) != 0U)
        {
            crc <<= 1U;
            crc ^= CRC16_CCITT_FALSE_POLYNOMIAL;
        }
        else
        {
            crc = (uint16_t)(crc << 1);
        }
    }

    return crc;
}

/**
 * @brief Initialize CRC context.
 *
 * @param ctx Pointer to CRC context.
 *
 * @return CRC16_Status_t
 * @retval CRC16_SUCCESS Context initialized with the configured initial value.
 * @retval CRC16_ERROR_NULL_POINTER ctx is NULL.
 */
CRC16_Status_t crc16Init(CRC16_Context_t *ctx)
{
    CRC16_Status_t status = validateContext(ctx);

    if (status != CRC16_SUCCESS)
    {
        return status;
    }

    ctx->currentCRC = (uint16_t)CRC16_INITIAL_VALUE;

    return CRC16_SUCCESS;
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
 * @return CRC16_Status_t
 * @retval CRC16_SUCCESS Buffer processed and ctx updated (len == 0 is a
 *         valid no-op; the context's CRC state is left unchanged).
 * @retval CRC16_ERROR_NULL_POINTER ctx is NULL, or data is NULL while len > 0.
 */
CRC16_Status_t crc16Update(CRC16_Context_t *ctx, const uint8_t *data, size_t len)
{
    CRC16_Status_t status = validateContext(ctx);
    size_t i;

    if (status != CRC16_SUCCESS)
    {
        return status;
    }

    status = validateBuffer(data, len);
    if (status != CRC16_SUCCESS)
    {
        return status;
    }

    for (i = 0U; i < len; i++)
    {
        ctx->currentCRC = crc16ProcessByte(ctx->currentCRC, data[i]);
    }

    return CRC16_SUCCESS;
}

/**
 * @brief Get current CRC value.
 *
 * Does not invalidate the context. Additional updates are allowed after this call.
 *
 * @param ctx CRC context.
 * @param crc Pointer to store the calculated CRC value.
 *
 * @return CRC16_Status_t
 * @retval CRC16_SUCCESS CRC value written to *crc.
 * @retval CRC16_ERROR_NULL_POINTER ctx or crc is NULL.
 */
CRC16_Status_t crc16GetResult(const CRC16_Context_t *ctx, uint16_t *crc)
{
    CRC16_Status_t status = validateContext(ctx);

    if (status != CRC16_SUCCESS)
    {
        return status;
    }

    if (crc == NULL)
    {
        return CRC16_ERROR_NULL_POINTER;
    }

    /* CRC16-CCITT-FALSE has no final XOR (XorOut = 0x0000), so the raw
     * accumulator is the final result. */
    *crc = ctx->currentCRC;

    return CRC16_SUCCESS;
}

/**
 * @brief Calculate CRC for a complete buffer.
 *
 * Thin wrapper around crc16Init(), crc16Update(), crc16GetResult() -- no CRC
 * logic is duplicated between the streaming and one-shot APIs.
 *
 * @param data Input buffer.
 * @param len  Number of bytes.
 * @param crc  Pointer to store the calculated CRC value.
 *
 * @return CRC16_Status_t
 * @retval CRC16_SUCCESS CRC value written to *crc.
 * @retval CRC16_ERROR_NULL_POINTER crc is NULL, or data is NULL while len > 0.
 */
CRC16_Status_t crc16Compute(const uint8_t *data, size_t len, uint16_t *crc)
{
    CRC16_Context_t ctx;
    CRC16_Status_t status;

    if (crc == NULL)
    {
        return CRC16_ERROR_NULL_POINTER;
    }

    status = crc16Init(&ctx);
    if (status != CRC16_SUCCESS)
    {
        return status;
    }

    status = crc16Update(&ctx, data, len);
    if (status != CRC16_SUCCESS)
    {
        return status;
    }

    return crc16GetResult(&ctx, crc);
}

/**
 * @brief Verify received CRC against calculated CRC.
 *
 * @param data        Input buffer.
 * @param len         Number of bytes.
 * @param receivedCRC CRC received from communication interface.
 *
 * @return CRC16_Status_t
 * @retval CRC16_SUCCESS CRC matches.
 * @retval CRC16_ERROR_MISMATCH Calculated CRC does not match receivedCRC.
 * @retval CRC16_ERROR_NULL_POINTER data is NULL while len > 0.
 */
CRC16_Status_t crc16Verify(const uint8_t *data, size_t len, uint16_t receivedCRC)
{
    uint16_t calculatedCRC = 0U;
    CRC16_Status_t status = crc16Compute(data, len, &calculatedCRC);

    if (status != CRC16_SUCCESS)
    {
        return status;
    }

    if (calculatedCRC != receivedCRC)
    {
        return CRC16_ERROR_MISMATCH;
    }

    return CRC16_SUCCESS;
}

/*****************************************End of crc16.c*****************************************/