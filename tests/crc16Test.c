/******************************************************************************
 * @file   crc16Test.c
 * @brief  Unit tests for CRC-16/CCITT-FALSE checksum calculation (crc16.h).
 *
 * @details
 * crc16.c uses a context-based streaming API (crc16Init/Update/GetResult)
 * plus one-shot convenience wrappers (crc16Compute/crc16Verify), all
 * returning CRC16_Status_t. There is no free-standing crc16Calculate() or
 * a crc16Update() that takes/returns a raw uint16_t -- every call goes
 * through a CRC16_Context_t and a status code, same as bitUtils.
 *
 * @author Abhishek Doddagoudar
 * @date   July 2026
 ******************************************************************************/

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <crc16.h>

static int totalTests = 0;
static int passedTests = 0;

/* ------------------------------------------------------------------------
 * Tiny test framework
 * ------------------------------------------------------------------------ */

#define TEST_ASSERT_TRUE(expr, desc)                          \
    do                                                        \
    {                                                         \
        totalTests++;                                         \
        if (expr)                                             \
        {                                                     \
            printf("  PASS (line %d): %s\n", __LINE__, desc); \
            passedTests++;                                    \
        }                                                     \
        else                                                  \
        {                                                     \
            printf("  FAIL (line %d): %s\n", __LINE__, desc); \
        }                                                     \
    } while (0)

#define TEST_ASSERT_EQ(expected, actual, desc)                             \
    do                                                                     \
    {                                                                      \
        totalTests++;                                                      \
        uint16_t expectedVal_ = (uint16_t)(expected);                      \
        uint16_t actualVal_ = (uint16_t)(actual);                          \
        if (expectedVal_ == actualVal_)                                    \
        {                                                                  \
            printf("  PASS (line %d): %s\n", __LINE__, desc);              \
            passedTests++;                                                 \
        }                                                                  \
        else                                                               \
        {                                                                  \
            printf("  FAIL (line %d): %s (expected 0x%04X, got 0x%04X)\n", \
                   __LINE__, desc, expectedVal_, actualVal_);              \
        }                                                                  \
    } while (0)

#define TEST_ASSERT_STATUS(expected, actual, desc)                            \
    do                                                                        \
    {                                                                         \
        totalTests++;                                                         \
        CRC16_Status_t expectedStatus_ = (expected);                          \
        CRC16_Status_t actualStatus_ = (actual);                              \
        if (expectedStatus_ == actualStatus_)                                 \
        {                                                                     \
            printf("  PASS (line %d): %s\n", __LINE__, desc);                 \
            passedTests++;                                                    \
        }                                                                     \
        else                                                                  \
        {                                                                     \
            printf("  FAIL (line %d): %s (expected status %d, got %d)\n",     \
                   __LINE__, desc, (int)expectedStatus_, (int)actualStatus_); \
        }                                                                     \
    } while (0)

/* ------------------------------------------------------------------------
 * Known-answer test
 * ------------------------------------------------------------------------ */
static void testKnownVector(void)
{
    const uint8_t testData[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    uint16_t result = 0;
    CRC16_Status_t status;

    printf("\ncrc16Compute known-answer test:\n");

    /* Standard CRC-16/CCITT-FALSE check value for ASCII "123456789" is 0x29B1.
     * This is the canonical test vector confirming polynomial 0x1021,
     * initial value 0xFFFF, and non-reflected in/out are all correct. */
    status = crc16Compute(testData, sizeof(testData), &result);

    TEST_ASSERT_STATUS(CRC16_SUCCESS, status, "crc16Compute(\"123456789\"): status");
    TEST_ASSERT_EQ(0x29B1U, result, "CRC of \"123456789\" matches CCITT-FALSE check value 0x29B1");
}

/* ------------------------------------------------------------------------
 * Empty buffer
 * ------------------------------------------------------------------------ */
static void testEmptyBuffer(void)
{
    uint16_t result = 0;
    CRC16_Status_t status;

    printf("\ncrc16Compute empty buffer:\n");

    /* len == 0 is a valid no-op per the design doc -- CRC of an empty
     * message is well-defined and equals the initial value, since no
     * bytes are ever folded into the accumulator. */
    status = crc16Compute((const uint8_t *)"", 0, &result);

    TEST_ASSERT_STATUS(CRC16_SUCCESS, status, "crc16Compute with len=0: status");
    TEST_ASSERT_EQ(CRC16_INITIAL_VALUE, result, "CRC of a zero-length buffer equals the initial value (0xFFFF)");
}

/* ------------------------------------------------------------------------
 * NULL pointer handling
 * ------------------------------------------------------------------------ */
static void testNullPointer(void)
{
    CRC16_Context_t ctx;
    uint16_t result = 0xDEADU;
    CRC16_Status_t status;

    printf("\nNULL pointer handling:\n");

    status = crc16Init(NULL);
    TEST_ASSERT_STATUS(CRC16_ERROR_NULL_POINTER, status, "crc16Init(NULL): status");

    status = crc16Compute(NULL, 5, &result);
    TEST_ASSERT_STATUS(CRC16_ERROR_NULL_POINTER, status, "crc16Compute with NULL data, len=5 (non-empty): status");
    TEST_ASSERT_EQ(0xDEADU, result, "crc16Compute with NULL data, len=5: output untouched");

    status = crc16Compute((const uint8_t *)"x", 1, NULL);
    TEST_ASSERT_STATUS(CRC16_ERROR_NULL_POINTER, status, "crc16Compute with NULL crc output pointer: status");

    crc16Init(&ctx);
    status = crc16Update(&ctx, NULL, 5);
    TEST_ASSERT_STATUS(CRC16_ERROR_NULL_POINTER, status, "crc16Update with NULL data, len=5 (non-empty): status");

    status = crc16Update(NULL, (const uint8_t *)"x", 1);
    TEST_ASSERT_STATUS(CRC16_ERROR_NULL_POINTER, status, "crc16Update with NULL context: status");

    status = crc16GetResult(&ctx, NULL);
    TEST_ASSERT_STATUS(CRC16_ERROR_NULL_POINTER, status, "crc16GetResult with NULL output pointer: status");

    status = crc16GetResult(NULL, &result);
    TEST_ASSERT_STATUS(CRC16_ERROR_NULL_POINTER, status, "crc16GetResult with NULL context: status");
}

/* ------------------------------------------------------------------------
 * NULL data with len == 0 is a valid no-op, not an error
 * ------------------------------------------------------------------------ */
static void testNullDataZeroLengthIsOk(void)
{
    CRC16_Context_t ctx;
    uint16_t before = 0;
    uint16_t after = 0;
    CRC16_Status_t status;

    printf("\nNULL data with len=0 (no bytes to read) is treated as a valid no-op:\n");

    crc16Init(&ctx);
    crc16GetResult(&ctx, &before);

    status = crc16Update(&ctx, NULL, 0);
    TEST_ASSERT_STATUS(CRC16_SUCCESS, status, "crc16Update(ctx, NULL, 0): status");

    crc16GetResult(&ctx, &after);
    TEST_ASSERT_EQ(before, after, "crc16Update(ctx, NULL, 0) leaves the CRC state unchanged");
}

/* ------------------------------------------------------------------------
 * Single byte sanity check
 * ------------------------------------------------------------------------ */
static void testSingleByteChangesState(void)
{
    CRC16_Context_t ctx;
    uint8_t singleByte[] = {0x00U};
    uint16_t result = 0;

    printf("\ncrc16Update single-byte sanity:\n");

    crc16Init(&ctx);
    crc16Update(&ctx, singleByte, 1);
    crc16GetResult(&ctx, &result);

    /* Not an external spec value, but the CRC must differ from the initial
     * value after processing at least one byte -- this catches an
     * accidentally-empty loop body or a return-before-processing bug. */
    TEST_ASSERT_TRUE(result != CRC16_INITIAL_VALUE,
                     "CRC changes from the initial value after processing one byte");
}

/* ------------------------------------------------------------------------
 * Streaming API matches one-shot API
 * ------------------------------------------------------------------------ */
static void testUpdateMatchesComputeWholeBuffer(void)
{
    const uint8_t testData[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    CRC16_Context_t ctx;
    uint16_t viaCompute = 0;
    uint16_t viaStreaming = 0;

    printf("\ncrc16Init+Update+GetResult matches crc16Compute:\n");

    crc16Compute(testData, sizeof(testData), &viaCompute);

    crc16Init(&ctx);
    crc16Update(&ctx, testData, sizeof(testData));
    crc16GetResult(&ctx, &viaStreaming);

    TEST_ASSERT_EQ(viaCompute, viaStreaming,
                   "streaming Init/Update/GetResult equals one-shot crc16Compute for the same buffer");
}

/* ------------------------------------------------------------------------
 * Incremental chunking
 * ------------------------------------------------------------------------ */
static void testIncrementalChunking(void)
{
    const uint8_t testData[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    size_t total = sizeof(testData);
    uint16_t whole = 0;
    uint16_t chunked = 0;
    uint16_t byteByByte = 0;
    CRC16_Context_t ctx;
    size_t i;

    printf("\ncrc16Update incremental chunking:\n");

    crc16Compute(testData, total, &whole);

    /* Feed the same data in two uneven chunks. */
    crc16Init(&ctx);
    crc16Update(&ctx, testData, 4);
    crc16Update(&ctx, testData + 4, total - 4);
    crc16GetResult(&ctx, &chunked);

    TEST_ASSERT_EQ(whole, chunked,
                   "splitting input across two crc16Update calls gives the same result as one crc16Compute call");

    /* Feed one byte at a time. */
    crc16Init(&ctx);
    for (i = 0U; i < total; i++)
    {
        crc16Update(&ctx, &testData[i], 1);
    }
    crc16GetResult(&ctx, &byteByByte);

    TEST_ASSERT_EQ(whole, byteByByte,
                   "feeding data one byte at a time via crc16Update gives the same result as crc16Compute");
}

/* ------------------------------------------------------------------------
 * crc16GetResult does not invalidate the context
 * ------------------------------------------------------------------------ */
static void testGetResultDoesNotInvalidateContext(void)
{
    const uint8_t part1[] = {'1', '2', '3', '4'};
    const uint8_t part2[] = {'5', '6', '7', '8', '9'};
    CRC16_Context_t ctx;
    uint16_t midResult = 0;
    uint16_t finalResult = 0;
    uint16_t expectedFinal = 0;

    printf("\ncrc16GetResult can be called mid-stream without disturbing further updates:\n");

    crc16Compute((const uint8_t *)"123456789", 9, &expectedFinal);

    crc16Init(&ctx);
    crc16Update(&ctx, part1, sizeof(part1));
    crc16GetResult(&ctx, &midResult);        /* peek at the partial CRC */
    crc16Update(&ctx, part2, sizeof(part2)); /* keep feeding after the peek */
    crc16GetResult(&ctx, &finalResult);

    TEST_ASSERT_TRUE(midResult != CRC16_INITIAL_VALUE, "mid-stream result differs from the initial value");
    TEST_ASSERT_EQ(expectedFinal, finalResult,
                   "calling crc16GetResult mid-stream doesn't disturb subsequent crc16Update calls");
}

/* ------------------------------------------------------------------------
 * Negative cases: corruption/truncation/extension must change the CRC
 * ------------------------------------------------------------------------ */
static void testCorruptedKnownVectorDoesNotMatch(void)
{
    /* Same known vector as testKnownVector, but with the last character
     * flipped from '9' to '0'. A correct CRC implementation must produce a
     * different value -- otherwise the CRC fails to detect corruption. */
    const uint8_t corrupted[] = {'1', '2', '3', '4', '5', '6', '7', '8', '0'};
    uint16_t result = 0;

    printf("\nnegative: corrupted \"123456789\" must NOT match the known-good CRC:\n");

    crc16Compute(corrupted, sizeof(corrupted), &result);
    TEST_ASSERT_TRUE(result != 0x29B1U, "corrupting one character of the known vector changes the CRC");
}

static void testTruncatedBufferDoesNotMatch(void)
{
    const uint8_t testData[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    uint16_t fullCrc = 0;
    uint16_t truncatedCrc = 0;

    printf("\nnegative: truncated buffer must NOT match the full-buffer CRC:\n");

    crc16Compute(testData, sizeof(testData), &fullCrc);
    crc16Compute(testData, sizeof(testData) - 1, &truncatedCrc); /* drop the last byte */

    TEST_ASSERT_TRUE(fullCrc != truncatedCrc, "dropping the last byte changes the CRC (length matters, not just content)");
}

static void testAppendedByteDoesNotMatch(void)
{
    const uint8_t testData[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '\0'}; /* trailing NUL appended */
    uint16_t originalCrc = 0;
    uint16_t extendedCrc = 0;

    printf("\nnegative: appending an extra byte must NOT match the original CRC:\n");

    crc16Compute(testData, sizeof(testData) - 1, &originalCrc);
    crc16Compute(testData, sizeof(testData), &extendedCrc);

    TEST_ASSERT_TRUE(originalCrc != extendedCrc, "appending a trailing byte (even 0x00) changes the CRC");
}

static void testDifferentDataDifferentCrc(void)
{
    const uint8_t dataA[] = {0x01U, 0x02U, 0x03U};
    const uint8_t dataB[] = {0x01U, 0x02U, 0x04U}; /* last byte differs */
    uint16_t crcA = 0;
    uint16_t crcB = 0;

    printf("\ncrc16Compute distinguishes different inputs:\n");

    crc16Compute(dataA, sizeof(dataA), &crcA);
    crc16Compute(dataB, sizeof(dataB), &crcB);

    TEST_ASSERT_TRUE(crcA != crcB, "a single differing byte produces a different CRC");
}

/* ------------------------------------------------------------------------
 * crc16Verify
 * ------------------------------------------------------------------------ */
static void testVerify(void)
{
    const uint8_t testData[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    CRC16_Status_t status;

    printf("\ncrc16Verify:\n");

    status = crc16Verify(testData, sizeof(testData), 0x29B1U);
    TEST_ASSERT_STATUS(CRC16_SUCCESS, status, "crc16Verify with the correct CRC succeeds");

    status = crc16Verify(testData, sizeof(testData), 0x0000U);
    TEST_ASSERT_STATUS(CRC16_ERROR_MISMATCH, status, "crc16Verify with a wrong CRC reports mismatch");

    status = crc16Verify(NULL, 5, 0x29B1U);
    TEST_ASSERT_STATUS(CRC16_ERROR_NULL_POINTER, status, "crc16Verify with NULL data (non-empty length) reports NULL pointer, not mismatch");
}

int main(void)
{
    printf("Running crc16 tests...\n");

    testKnownVector();
    testEmptyBuffer();
    testNullPointer();
    testNullDataZeroLengthIsOk();
    testSingleByteChangesState();
    testUpdateMatchesComputeWholeBuffer();
    testIncrementalChunking();
    testGetResultDoesNotInvalidateContext();
    testCorruptedKnownVectorDoesNotMatch();
    testTruncatedBufferDoesNotMatch();
    testAppendedByteDoesNotMatch();
    testDifferentDataDifferentCrc();

    printf("\n%d / %d tests passed.\n", passedTests, totalTests);

    return (passedTests == totalTests) ? 0 : 1;
}

/**************************************** END OF crc16Test.c ****************************************/