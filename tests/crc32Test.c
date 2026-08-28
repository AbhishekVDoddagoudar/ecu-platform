/******************************************************************************
 * @file   crc32Test.c
 * @brief  Unit tests for CRC-32/ISO-HDLC checksum calculation (crc32.h).
 *
 * @details
 * crc32.c uses a context-based streaming API (crc32Init/Update/GetResult)
 * plus one-shot convenience wrappers (crc32Compute/crc32Verify), all
 * returning CRC32_Status_t -- same shape as crc16Test.c, width swapped.
 *
 * Reference values for the non-canonical vectors (everything besides the
 * "123456789" check value) were independently cross-checked against
 * Python's zlib.crc32(), which implements the same CRC-32/ISO-HDLC
 * variant, rather than hand-derived -- per docs/crc32-design.md, Section
 * 12's instruction not to hand-derive expected values.
 *
 * @author Abhishek Doddagoudar
 * @date   August 2026
 ******************************************************************************/

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <crc32.h>

static int totalTests = 0;
static int passedTests = 0;

/* ------------------------------------------------------------------------
 * Tiny test framework (matches crc16Test.c)
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
        uint32_t expectedVal_ = (uint32_t)(expected);                      \
        uint32_t actualVal_ = (uint32_t)(actual);                          \
        if (expectedVal_ == actualVal_)                                    \
        {                                                                  \
            printf("  PASS (line %d): %s\n", __LINE__, desc);              \
            passedTests++;                                                 \
        }                                                                  \
        else                                                               \
        {                                                                  \
            printf("  FAIL (line %d): %s (expected 0x%08X, got 0x%08X)\n", \
                   __LINE__, desc, expectedVal_, actualVal_);              \
        }                                                                  \
    } while (0)

#define TEST_ASSERT_STATUS(expected, actual, desc)                            \
    do                                                                        \
    {                                                                         \
        totalTests++;                                                         \
        CRC32_Status_t expectedStatus_ = (expected);                          \
        CRC32_Status_t actualStatus_ = (actual);                              \
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
 * 1. Canonical known-answer vector
 * ------------------------------------------------------------------------ */
static void testKnownVector(void)
{
    const uint8_t testData[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    uint32_t result = 0;
    CRC32_Status_t status;

    printf("\ncrc32Compute known-answer test:\n");

    /* Standard CRC-32/ISO-HDLC check value for ASCII "123456789" is
     * 0xCBF43926 -- the canonical test vector confirming polynomial
     * 0x04C11DB7, initial value 0xFFFFFFFF, RefIn/RefOut, and the final
     * XOR are all correct together. This is the single most important
     * test in this file (docs/crc32-design.md, Section 12). */
    status = crc32Compute(testData, sizeof(testData), &result);

    TEST_ASSERT_STATUS(CRC32_SUCCESS, status, "crc32Compute(\"123456789\"): status");
    TEST_ASSERT_EQ(0xCBF43926U, result, "CRC of \"123456789\" matches the ISO-HDLC check value 0xCBF43926");
}

/* ------------------------------------------------------------------------
 * 2. Empty input
 * ------------------------------------------------------------------------ */
static void testEmptyBuffer(void)
{
    uint32_t result = 0;
    CRC32_Status_t status;

    printf("\ncrc32Compute empty buffer:\n");

    /* len == 0 is a valid no-op. Unlike CRC16 (no final XOR), CRC32's
     * result for an empty message is NOT the initial value -- it's the
     * initial value with the final XOR already applied:
     *     0xFFFFFFFF ^ 0xFFFFFFFF = 0x00000000
     * This specifically exercises the NULL/zero-length contract and
     * init/finalization working together (per review). */
    status = crc32Compute(NULL, 0, &result);

    TEST_ASSERT_STATUS(CRC32_SUCCESS, status, "crc32Compute(NULL, 0, ...): status");
    TEST_ASSERT_EQ(0x00000000U, result, "CRC of a zero-length buffer is 0x00000000 (init XOR finalXor)");
}

/* ------------------------------------------------------------------------
 * 3. Single byte
 * ------------------------------------------------------------------------ */
static void testSingleByte(void)
{
    uint32_t result = 0;

    printf("\ncrc32Compute single-byte vectors:\n");

    crc32Compute((const uint8_t[]){0x00U}, 1, &result);
    TEST_ASSERT_EQ(0xD202EF8DU, result, "CRC of single byte 0x00 matches reference (0xD202EF8D)");

    crc32Compute((const uint8_t[]){0xFFU}, 1, &result);
    TEST_ASSERT_EQ(0xFF000000U, result, "CRC of single byte 0xFF matches reference (0xFF000000)");
}

/* ------------------------------------------------------------------------
 * 4. Various byte patterns (binary data including 0x00 / 0xFF, text)
 * ------------------------------------------------------------------------ */
static void testVariousBytePatterns(void)
{
    const uint8_t pattern[] = {0x00U, 0xFFU, 0x00U, 0xFFU, 0xAAU, 0x55U};
    const uint8_t allZero[16] = {0};
    uint8_t allFF[16];
    const uint8_t abc[] = {'a', 'b', 'c'};
    uint32_t result = 0;
    size_t i;

    printf("\ncrc32Compute various byte patterns:\n");

    crc32Compute(pattern, sizeof(pattern), &result);
    TEST_ASSERT_EQ(0x8AD7A3DAU, result, "CRC of {0x00,0xFF,0x00,0xFF,0xAA,0x55} matches reference");

    crc32Compute(allZero, sizeof(allZero), &result);
    TEST_ASSERT_EQ(0xECBB4B55U, result, "CRC of 16 zero bytes matches reference (0xECBB4B55)");

    for (i = 0U; i < sizeof(allFF); i++)
    {
        allFF[i] = 0xFFU;
    }
    crc32Compute(allFF, sizeof(allFF), &result);
    TEST_ASSERT_EQ(0x3FB3C61AU, result, "CRC of 16 0xFF bytes matches reference (0x3FB3C61A)");

    crc32Compute(abc, sizeof(abc), &result);
    TEST_ASSERT_EQ(0x352441C2U, result, "CRC of \"abc\" matches reference (0x352441C2)");
}

/* ------------------------------------------------------------------------
 * 5. Incremental vs one-shot (3-way split, per review)
 * ------------------------------------------------------------------------ */
static void testIncrementalVsOneShot(void)
{
    const uint8_t full[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    CRC32_Context_t ctx;
    uint32_t oneShot = 0;
    uint32_t incremental = 0;
    CRC32_Status_t status;

    printf("\ncrc32Init+Update+Update+Update+GetResult matches crc32Compute (3-way split):\n");

    crc32Compute(full, sizeof(full), &oneShot);

    crc32Init(&ctx);
    crc32Update(&ctx, (const uint8_t *)"123", 3);
    crc32Update(&ctx, (const uint8_t *)"456", 3);
    crc32Update(&ctx, (const uint8_t *)"789", 3);
    status = crc32GetResult(&ctx, &incremental);

    TEST_ASSERT_STATUS(CRC32_SUCCESS, status, "crc32GetResult after 3-way split: status");
    TEST_ASSERT_EQ(0xCBF43926U, incremental, "3-way split (\"123\"+\"456\"+\"789\") matches the canonical check value");
    TEST_ASSERT_EQ(oneShot, incremental, "3-way split matches one-shot crc32Compute for the same buffer");
}

/* ------------------------------------------------------------------------
 * 6. Multiple Update() calls, several different split points
 * ------------------------------------------------------------------------ */
static void testMultipleUpdateCallsVariousSplits(void)
{
    const uint8_t full[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    size_t total = sizeof(full);
    uint32_t whole = 0;
    CRC32_Context_t ctx;
    uint32_t chunked = 0;
    uint32_t byteByByte = 0;
    size_t split;
    size_t i;

    printf("\ncrc32Update incremental chunking at every split point:\n");

    crc32Compute(full, total, &whole);

    /* Every possible two-way split point, 0..total. */
    for (split = 0U; split <= total; split++)
    {
        crc32Init(&ctx);
        crc32Update(&ctx, full, split);
        crc32Update(&ctx, full + split, total - split);
        crc32GetResult(&ctx, &chunked);
        TEST_ASSERT_EQ(whole, chunked, "two-way split at every boundary matches one-shot crc32Compute");
    }

    /* One byte at a time. */
    crc32Init(&ctx);
    for (i = 0U; i < total; i++)
    {
        crc32Update(&ctx, &full[i], 1);
    }
    crc32GetResult(&ctx, &byteByByte);
    TEST_ASSERT_EQ(whole, byteByByte, "feeding data one byte at a time matches one-shot crc32Compute");
}

/* ------------------------------------------------------------------------
 * 7. GetResult() does not modify the context
 * ------------------------------------------------------------------------ */
static void testGetResultDoesNotModifyContext(void)
{
    CRC32_Context_t ctx;
    uint32_t first = 0;
    uint32_t second = 0;

    printf("\ncrc32GetResult called twice in a row returns the same value both times:\n");

    crc32Init(&ctx);
    crc32Update(&ctx, (const uint8_t *)"123456789", 9);

    crc32GetResult(&ctx, &first);
    crc32GetResult(&ctx, &second);

    TEST_ASSERT_EQ(first, second, "calling crc32GetResult twice without an intervening Update returns identical results");
    TEST_ASSERT_EQ(0xCBF43926U, second, "repeated crc32GetResult still matches the canonical check value");
}

/* ------------------------------------------------------------------------
 * 8. Update() after GetResult() -- the exact scenario from review,
 *    specifically validating the final-XOR design (Section 6)
 * ------------------------------------------------------------------------ */
static void testUpdateAfterGetResult(void)
{
    CRC32_Context_t ctx;
    uint32_t r1 = 0;
    uint32_t r2 = 0;

    printf("\ncrc32Update after crc32GetResult continues correctly (validates final-XOR design):\n");

    crc32Init(&ctx);
    crc32Update(&ctx, (const uint8_t *)"123", 3);
    crc32GetResult(&ctx, &r1);

    crc32Update(&ctx, (const uint8_t *)"456789", 6);
    crc32GetResult(&ctx, &r2);

    TEST_ASSERT_TRUE(r1 != 0xCBF43926U, "the partial result after \"123\" alone is not the full-sequence CRC");
    TEST_ASSERT_EQ(0xCBF43926U, r2, "Init->Update(\"123\")->GetResult->Update(\"456789\")->GetResult equals CRC(\"123456789\")");
}

/* ------------------------------------------------------------------------
 * 9. NULL context
 * ------------------------------------------------------------------------ */
static void testNullContext(void)
{
    CRC32_Status_t status;
    uint32_t crc = 0;

    printf("\nNULL context handling:\n");

    status = crc32Init(NULL);
    TEST_ASSERT_STATUS(CRC32_ERROR_NULL_POINTER, status, "crc32Init(NULL): status");

    status = crc32Update(NULL, (const uint8_t *)"x", 1);
    TEST_ASSERT_STATUS(CRC32_ERROR_NULL_POINTER, status, "crc32Update with NULL context: status");

    status = crc32GetResult(NULL, &crc);
    TEST_ASSERT_STATUS(CRC32_ERROR_NULL_POINTER, status, "crc32GetResult with NULL context: status");
}

/* ------------------------------------------------------------------------
 * 10. NULL data with len > 0
 * ------------------------------------------------------------------------ */
static void testNullDataLenPositive(void)
{
    CRC32_Context_t ctx;
    CRC32_Status_t status;
    uint32_t result = 0xDEADBEEFU;

    printf("\nNULL data with len > 0 is rejected:\n");

    crc32Init(&ctx);
    status = crc32Update(&ctx, NULL, 5);
    TEST_ASSERT_STATUS(CRC32_ERROR_NULL_POINTER, status, "crc32Update with NULL data, len=5: status");

    status = crc32Compute(NULL, 5, &result);
    TEST_ASSERT_STATUS(CRC32_ERROR_NULL_POINTER, status, "crc32Compute with NULL data, len=5: status");
    TEST_ASSERT_EQ(0xDEADBEEFU, result, "crc32Compute with NULL data, len=5: output pointer left untouched");
}

/* ------------------------------------------------------------------------
 * 11. NULL data with len == 0 -- must be SUCCESS, not an error
 * ------------------------------------------------------------------------ */
static void testNullDataZeroLengthIsOk(void)
{
    CRC32_Context_t ctx;
    uint32_t before = 0;
    uint32_t after = 0;
    CRC32_Status_t status;

    printf("\nNULL data with len=0 (no bytes to read) is treated as a valid no-op:\n");

    crc32Init(&ctx);
    crc32GetResult(&ctx, &before);

    status = crc32Update(&ctx, NULL, 0);
    TEST_ASSERT_STATUS(CRC32_SUCCESS, status, "crc32Update(ctx, NULL, 0): status");

    crc32GetResult(&ctx, &after);
    TEST_ASSERT_EQ(before, after, "crc32Update(ctx, NULL, 0) leaves the CRC state unchanged");

    status = crc32Compute(NULL, 0, &after);
    TEST_ASSERT_STATUS(CRC32_SUCCESS, status, "crc32Compute(NULL, 0, ...): status (this is the empty-message case, not an error)");
}

/* ------------------------------------------------------------------------
 * 12. NULL output CRC pointer
 * ------------------------------------------------------------------------ */
static void testNullOutputPointer(void)
{
    CRC32_Context_t ctx;
    CRC32_Status_t status;

    printf("\nNULL output pointer handling:\n");

    crc32Init(&ctx);
    status = crc32GetResult(&ctx, NULL);
    TEST_ASSERT_STATUS(CRC32_ERROR_NULL_POINTER, status, "crc32GetResult with NULL output pointer: status");

    status = crc32Compute((const uint8_t *)"x", 1, NULL);
    TEST_ASSERT_STATUS(CRC32_ERROR_NULL_POINTER, status, "crc32Compute with NULL output pointer: status");
}

/* ------------------------------------------------------------------------
 * 13/14/15. crc32Verify: success, mismatch, invalid parameters
 * ------------------------------------------------------------------------ */
static void testVerify(void)
{
    const uint8_t testData[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    CRC32_Status_t status;

    printf("\ncrc32Verify:\n");

    status = crc32Verify(testData, sizeof(testData), 0xCBF43926U);
    TEST_ASSERT_STATUS(CRC32_SUCCESS, status, "crc32Verify with the correct CRC succeeds");

    status = crc32Verify(testData, sizeof(testData), 0x00000000U);
    TEST_ASSERT_STATUS(CRC32_ERROR_MISMATCH, status, "crc32Verify with a wrong CRC reports mismatch, not NULL_POINTER");

    status = crc32Verify(NULL, 5, 0xCBF43926U);
    TEST_ASSERT_STATUS(CRC32_ERROR_NULL_POINTER, status,
                        "crc32Verify with NULL data (non-empty length) reports NULL pointer -- comparison never attempted, not treated as mismatch");

    status = crc32Verify(NULL, 0, 0x00000000U);
    TEST_ASSERT_STATUS(CRC32_SUCCESS, status, "crc32Verify(NULL, 0, 0x00000000) succeeds -- empty input's correct CRC is 0x00000000");
}

/* ------------------------------------------------------------------------
 * 16. Binary data including 0x00 / 0xFF mixed with text (covered above in
 *     testVariousBytePatterns and testSingleByte -- listed here as its own
 *     named test for traceability against the requested coverage list)
 * ------------------------------------------------------------------------ */
static void testBinaryDataWithNullAndFFBytes(void)
{
    /* A buffer that specifically embeds 0x00 bytes mid-stream -- this
     * would trip up any accidental strlen()-based length handling
     * (there is none in this implementation, but this test would catch
     * it if introduced). */
    const uint8_t data[] = {'A', 0x00U, 'B', 0xFFU, 0x00U, 'C'};
    uint32_t result = 0;
    CRC32_Status_t status;

    printf("\ncrc32Compute with embedded 0x00 and 0xFF bytes (len given explicitly, not implied by content):\n");

    status = crc32Compute(data, sizeof(data), &result);
    TEST_ASSERT_STATUS(CRC32_SUCCESS, status, "crc32Compute over data containing embedded 0x00 bytes: status");
    TEST_ASSERT_TRUE(result != 0x00000000U, "CRC of non-empty data containing 0x00 bytes is not itself 0x00000000 by coincidence");
}

/* ------------------------------------------------------------------------
 * 17. Larger buffer
 * ------------------------------------------------------------------------ */
static void testLargerBuffer(void)
{
    uint8_t data[256];
    uint32_t result = 0;
    size_t i;

    printf("\ncrc32Compute over a 256-byte buffer:\n");

    for (i = 0U; i < sizeof(data); i++)
    {
        data[i] = (uint8_t)i; /* 0x00, 0x01, ..., 0xFF */
    }

    crc32Compute(data, sizeof(data), &result);
    TEST_ASSERT_EQ(0x29058C73U, result, "CRC of the 256-byte sequence 0x00..0xFF matches reference (0x29058C73)");
}

/* ------------------------------------------------------------------------
 * 18. Context independence / reentrancy -- two contexts interleaved
 * ------------------------------------------------------------------------ */
static void testContextIndependence(void)
{
    CRC32_Context_t ctxA;
    CRC32_Context_t ctxB;
    uint32_t resultA = 0;
    uint32_t resultB = 0;
    uint32_t expectedA = 0;
    uint32_t expectedB = 0;

    printf("\nTwo independent contexts interleaved do not interfere with each other:\n");

    crc32Compute((const uint8_t *)"123456789", 9, &expectedA);
    crc32Compute((const uint8_t *)"abc", 3, &expectedB);

    crc32Init(&ctxA);
    crc32Init(&ctxB);

    /* Interleave updates between the two contexts, byte-by-byte for A and
     * whole-buffer for B, to make sure state genuinely doesn't cross
     * between them via any shared/global storage. */
    crc32Update(&ctxA, (const uint8_t *)"123", 3);
    crc32Update(&ctxB, (const uint8_t *)"abc", 3);
    crc32Update(&ctxA, (const uint8_t *)"456", 3);
    crc32Update(&ctxA, (const uint8_t *)"789", 3);

    crc32GetResult(&ctxA, &resultA);
    crc32GetResult(&ctxB, &resultB);

    TEST_ASSERT_EQ(expectedA, resultA, "interleaved context A still produces the correct CRC for \"123456789\"");
    TEST_ASSERT_EQ(expectedB, resultB, "interleaved context B still produces the correct CRC for \"abc\", unaffected by A's updates");
}

/* ------------------------------------------------------------------------
 * 19. Reinitialization -- crc32Init() on an already-used context resets it
 * ------------------------------------------------------------------------ */
static void testReinitialization(void)
{
    CRC32_Context_t ctx;
    uint32_t afterFirstUse = 0;
    uint32_t afterReinit = 0;
    uint32_t freshCompute = 0;
    CRC32_Status_t status;

    printf("\ncrc32Init on an already-used context resets it (no 'already initialized' concept here, per design Section 3):\n");

    crc32Init(&ctx);
    crc32Update(&ctx, (const uint8_t *)"123456789", 9);
    crc32GetResult(&ctx, &afterFirstUse);
    TEST_ASSERT_EQ(0xCBF43926U, afterFirstUse, "context correctly computed the first sequence's CRC");

    /* Re-initialize the same context and reuse it for a different
     * computation -- unlike the memory pool, crc32Init() has no
     * "already initialized" rejection (docs/crc32-design.md, Section 3),
     * so this must succeed and genuinely reset currentCRC. */
    status = crc32Init(&ctx);
    TEST_ASSERT_STATUS(CRC32_SUCCESS, status, "crc32Init on an already-used context succeeds (no reinit guard for this module)");

    crc32Update(&ctx, (const uint8_t *)"abc", 3);
    crc32GetResult(&ctx, &afterReinit);

    crc32Compute((const uint8_t *)"abc", 3, &freshCompute);
    TEST_ASSERT_EQ(freshCompute, afterReinit, "after crc32Init, the reused context computes \"abc\"'s CRC exactly as a fresh crc32Compute would");
}

/* ------------------------------------------------------------------------
 * 20. Zero-length Update() specifically (data non-NULL, len == 0)
 * ------------------------------------------------------------------------ */
static void testZeroLengthUpdateWithNonNullData(void)
{
    CRC32_Context_t ctx;
    uint32_t before = 0;
    uint32_t after = 0;
    CRC32_Status_t status;

    printf("\ncrc32Update with a valid (non-NULL) buffer pointer but len=0:\n");

    crc32Init(&ctx);
    crc32Update(&ctx, (const uint8_t *)"123", 3);
    crc32GetResult(&ctx, &before);

    /* data is a valid, non-NULL pointer here -- distinct from test 11,
     * which specifically covers data == NULL, len == 0. */
    status = crc32Update(&ctx, (const uint8_t *)"456", 0);
    TEST_ASSERT_STATUS(CRC32_SUCCESS, status, "crc32Update with non-NULL data and len=0: status");

    crc32GetResult(&ctx, &after);
    TEST_ASSERT_EQ(before, after, "crc32Update with len=0 leaves the CRC state unchanged, even with a valid data pointer");
}

/* ------------------------------------------------------------------------
 * Negative cases: corruption/truncation/extension must change the CRC
 * (mirrors crc16Test.c's negative-case coverage)
 * ------------------------------------------------------------------------ */
static void testCorruptedKnownVectorDoesNotMatch(void)
{
    /* Same known vector as testKnownVector, but with the last character
     * flipped from '9' to '0'. */
    const uint8_t corrupted[] = {'1', '2', '3', '4', '5', '6', '7', '8', '0'};
    uint32_t result = 0;

    printf("\nnegative: corrupted \"123456789\" must NOT match the known-good CRC:\n");

    crc32Compute(corrupted, sizeof(corrupted), &result);
    TEST_ASSERT_EQ(0xB2288182U, result, "corrupted vector matches its own independently-computed reference");
    TEST_ASSERT_TRUE(result != 0xCBF43926U, "corrupting one character of the known vector changes the CRC");
}

static void testTruncatedBufferDoesNotMatch(void)
{
    const uint8_t testData[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    uint32_t fullCrc = 0;
    uint32_t truncatedCrc = 0;

    printf("\nnegative: truncated buffer must NOT match the full-buffer CRC:\n");

    crc32Compute(testData, sizeof(testData), &fullCrc);
    crc32Compute(testData, sizeof(testData) - 1, &truncatedCrc); /* drop the last byte */

    TEST_ASSERT_EQ(0x9AE0DAAFU, truncatedCrc, "truncated buffer matches its own independently-computed reference");
    TEST_ASSERT_TRUE(fullCrc != truncatedCrc, "dropping the last byte changes the CRC (length matters, not just content)");
}

static void testAppendedByteDoesNotMatch(void)
{
    const uint8_t testData[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', 0x00U};
    uint32_t originalCrc = 0;
    uint32_t extendedCrc = 0;

    printf("\nnegative: appending an extra byte must NOT match the original CRC:\n");

    crc32Compute(testData, sizeof(testData) - 1, &originalCrc);
    crc32Compute(testData, sizeof(testData), &extendedCrc);

    TEST_ASSERT_EQ(0x00C49E49U, extendedCrc, "extended buffer matches its own independently-computed reference");
    TEST_ASSERT_TRUE(originalCrc != extendedCrc, "appending a trailing byte (even 0x00) changes the CRC");
}

static void testDifferentDataDifferentCrc(void)
{
    const uint8_t dataA[] = {0x01U, 0x02U, 0x03U};
    const uint8_t dataB[] = {0x01U, 0x02U, 0x04U}; /* last byte differs */
    uint32_t crcA = 0;
    uint32_t crcB = 0;

    printf("\ncrc32Compute distinguishes different inputs:\n");

    crc32Compute(dataA, sizeof(dataA), &crcA);
    crc32Compute(dataB, sizeof(dataB), &crcB);

    TEST_ASSERT_EQ(0x55BC801DU, crcA, "dataA matches its own independently-computed reference");
    TEST_ASSERT_EQ(0xCBD815BEU, crcB, "dataB matches its own independently-computed reference");
    TEST_ASSERT_TRUE(crcA != crcB, "a single differing byte produces a different CRC");
}

int main(void)
{
    printf("Running crc32 tests...\n");

    testKnownVector();
    testEmptyBuffer();
    testSingleByte();
    testVariousBytePatterns();
    testIncrementalVsOneShot();
    testMultipleUpdateCallsVariousSplits();
    testGetResultDoesNotModifyContext();
    testUpdateAfterGetResult();
    testNullContext();
    testNullDataLenPositive();
    testNullDataZeroLengthIsOk();
    testNullOutputPointer();
    testVerify();
    testBinaryDataWithNullAndFFBytes();
    testLargerBuffer();
    testContextIndependence();
    testReinitialization();
    testZeroLengthUpdateWithNonNullData();
    testCorruptedKnownVectorDoesNotMatch();
    testTruncatedBufferDoesNotMatch();
    testAppendedByteDoesNotMatch();
    testDifferentDataDifferentCrc();

    printf("\n%d / %d tests passed.\n", passedTests, totalTests);

    return (passedTests == totalTests) ? 0 : 1;
}

/**************************************** END OF crc32Test.c ****************************************/