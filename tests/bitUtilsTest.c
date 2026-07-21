/******************************************************************************
 * @file   testBitUtils.c
 * @brief  Unit tests for bit manipulation utilities (bitUtils.h).
 *
 * @details
 * bitUtils.c validates all preconditions via return codes (BitUtilsStatus_t),
 * not assert(). There is nothing to fork/trap here -- every negative case is
 * just a normal call whose return value we check, same as the happy path.
 *
 * @author Abhishek Doddagoudar
 * @date   July 2026
 ******************************************************************************/

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <bitUtils.h>

static int totalTests = 0;
static int passedTests = 0;
static int currentSectionFailed = 0;

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
            currentSectionFailed = 1;                         \
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
            currentSectionFailed = 1;                                      \
        }                                                                  \
    } while (0)

#define TEST_ASSERT_STATUS(expected, actual, desc)                            \
    do                                                                        \
    {                                                                         \
        totalTests++;                                                         \
        BitUtilsStatus_t expectedStatus_ = (expected);                        \
        BitUtilsStatus_t actualStatus_ = (actual);                            \
        if (expectedStatus_ == actualStatus_)                                 \
        {                                                                     \
            printf("  PASS (line %d): %s\n", __LINE__, desc);                 \
            passedTests++;                                                    \
        }                                                                     \
        else                                                                  \
        {                                                                     \
            printf("  FAIL (line %d): %s (expected status %d, got %d)\n",     \
                   __LINE__, desc, (int)expectedStatus_, (int)actualStatus_); \
            currentSectionFailed = 1;                                         \
        }                                                                     \
    } while (0)

static void beginSection(const char *name)
{
    currentSectionFailed = 0;
    printf("\n%s\n", name);
}

/* ------------------------------------------------------------------------
 * 1. bitUtilsSetBit
 * ------------------------------------------------------------------------ */
static void testSetBit(void)
{
    uint32_t out = 0;
    BitUtilsStatus_t status;

    beginSection("bitUtilsSetBit:");

    status = bitUtilsSetBit(0x00000000U, 0, &out);
    TEST_ASSERT_STATUS(BITUTILS_SUCCESS, status, "set bit 0 on zero: status");
    TEST_ASSERT_EQ(0x00000001U, out, "set bit 0 on zero: value");

    status = bitUtilsSetBit(0x00000000U, 31, &out);
    TEST_ASSERT_STATUS(BITUTILS_SUCCESS, status, "set bit 31 (MSB) on zero: status");
    TEST_ASSERT_EQ(0x80000000U, out, "set bit 31 (MSB) on zero: value");

    out = 0xDEADBEEFU; /* poison value to prove the no-op branch doesn't touch it wrongly */
    status = bitUtilsSetBit(0x0000000AU, 1, &out);
    TEST_ASSERT_STATUS(BITUTILS_SUCCESS, status, "set already-set bit: status");
    TEST_ASSERT_EQ(0x0000000AU, out, "set already-set bit is a no-op (0xA stays 0xA)");

    out = 0xDEADBEEFU;
    status = bitUtilsSetBit(0x00000000U, 32, &out);
    TEST_ASSERT_STATUS(BITUTILS_ERROR_INVALID_BIT_POSITION, status, "invalid bit position 32: status");
    TEST_ASSERT_EQ(0xDEADBEEFU, out, "invalid bit position 32: output untouched");

    status = bitUtilsSetBit(0x00000000U, 255, NULL);
    TEST_ASSERT_STATUS(BITUTILS_ERROR_NULL_POINTER, status, "NULL output pointer takes precedence over garbage bit position");

    status = bitUtilsSetBit(0x00000000U, 5, NULL);
    TEST_ASSERT_STATUS(BITUTILS_ERROR_NULL_POINTER, status, "NULL output pointer with valid bit position");
}

/* ------------------------------------------------------------------------
 * 2. bitUtilsClearBit
 * ------------------------------------------------------------------------ */
static void testClearBit(void)
{
    uint32_t out = 0;
    BitUtilsStatus_t status;

    beginSection("bitUtilsClearBit:");

    status = bitUtilsClearBit(0xFFFFFFFFU, 0, &out);
    TEST_ASSERT_STATUS(BITUTILS_SUCCESS, status, "clear bit 0 on all-ones: status");
    TEST_ASSERT_EQ(0xFFFFFFFEU, out, "clear bit 0 on all-ones: value");

    status = bitUtilsClearBit(0xFFFFFFFFU, 31, &out);
    TEST_ASSERT_STATUS(BITUTILS_SUCCESS, status, "clear bit 31 (MSB) on all-ones: status");
    TEST_ASSERT_EQ(0x7FFFFFFFU, out, "clear bit 31 (MSB) on all-ones: value");

    status = bitUtilsClearBit(0x0000000AU, 0, &out);
    TEST_ASSERT_STATUS(BITUTILS_SUCCESS, status, "clear already-clear bit: status");
    TEST_ASSERT_EQ(0x0000000AU, out, "clear already-clear bit is a no-op");

    out = 0xDEADBEEFU;
    status = bitUtilsClearBit(0xFFFFFFFFU, 32, &out);
    TEST_ASSERT_STATUS(BITUTILS_ERROR_INVALID_BIT_POSITION, status, "invalid bit position 32: status");
    TEST_ASSERT_EQ(0xDEADBEEFU, out, "invalid bit position 32: output untouched (regression guard for the dropped-status bug)");

    status = bitUtilsClearBit(0xFFFFFFFFU, 5, NULL);
    TEST_ASSERT_STATUS(BITUTILS_ERROR_NULL_POINTER, status, "NULL output pointer");
}

/* ------------------------------------------------------------------------
 * 3. bitUtilsToggleBit
 * ------------------------------------------------------------------------ */
static void testToggleBit(void)
{
    uint32_t out = 0;
    BitUtilsStatus_t status;

    beginSection("bitUtilsToggleBit:");

    status = bitUtilsToggleBit(0x00000000U, 4, &out);
    TEST_ASSERT_STATUS(BITUTILS_SUCCESS, status, "toggle 0 -> 1: status");
    TEST_ASSERT_EQ(0x00000010U, out, "toggle 0 -> 1: value");

    status = bitUtilsToggleBit(0x00000010U, 4, &out);
    TEST_ASSERT_STATUS(BITUTILS_SUCCESS, status, "toggle 1 -> 0: status");
    TEST_ASSERT_EQ(0x00000000U, out, "toggle 1 -> 0: value");

    status = bitUtilsToggleBit(0x00000000U, 31, &out);
    TEST_ASSERT_STATUS(BITUTILS_SUCCESS, status, "toggle MSB: status");
    TEST_ASSERT_EQ(0x80000000U, out, "toggle MSB: value");

    out = 0xDEADBEEFU;
    status = bitUtilsToggleBit(0x00000000U, 32, &out);
    TEST_ASSERT_STATUS(BITUTILS_ERROR_INVALID_BIT_POSITION, status, "invalid bit position 32: status");
    TEST_ASSERT_EQ(0xDEADBEEFU, out, "invalid bit position 32: output untouched");

    status = bitUtilsToggleBit(0x00000000U, 5, NULL);
    TEST_ASSERT_STATUS(BITUTILS_ERROR_NULL_POINTER, status, "NULL output pointer");
}

/* ------------------------------------------------------------------------
 * 4. bitUtilsIsBitSet
 * ------------------------------------------------------------------------ */
static void testIsBitSet(void)
{
    bool isSet = false;
    BitUtilsStatus_t status;

    beginSection("bitUtilsIsBitSet:");

    status = bitUtilsIsBitSet(0x0000000AU, 1, &isSet);
    TEST_ASSERT_STATUS(BITUTILS_SUCCESS, status, "bit set: status");
    TEST_ASSERT_TRUE(isSet == true, "bit 1 set in 0xA");

    status = bitUtilsIsBitSet(0x0000000AU, 0, &isSet);
    TEST_ASSERT_STATUS(BITUTILS_SUCCESS, status, "bit clear: status");
    TEST_ASSERT_TRUE(isSet == false, "bit 0 clear in 0xA");

    status = bitUtilsIsBitSet(0x80000000U, 31, &isSet);
    TEST_ASSERT_STATUS(BITUTILS_SUCCESS, status, "bit 31: status");
    TEST_ASSERT_TRUE(isSet == true, "MSB (bit 31) detected as set");

    isSet = true; /* poison to prove untouched on error */
    status = bitUtilsIsBitSet(0x00000000U, 32, &isSet);
    TEST_ASSERT_STATUS(BITUTILS_ERROR_INVALID_BIT_POSITION, status, "invalid bit position 32: status");
    TEST_ASSERT_TRUE(isSet == true, "invalid bit position 32: output untouched (regression guard)");

    status = bitUtilsIsBitSet(0x00000000U, 5, NULL);
    TEST_ASSERT_STATUS(BITUTILS_ERROR_NULL_POINTER, status, "NULL output pointer");
}

/* ------------------------------------------------------------------------
 * 5. bitUtilsExtractBits
 * ------------------------------------------------------------------------ */
static void testExtractBits(void)
{
    uint32_t extracted = 0;
    BitUtilsStatus_t status;

    beginSection("bitUtilsExtractBits:");

    /* Spec example: value 11010110b, start=2, length=3 -> expected 101b (0x5) */
    status = bitUtilsExtractBits(0xD6U, 2, 3, &extracted);
    TEST_ASSERT_STATUS(BITUTILS_SUCCESS, status, "spec example 11010110 start=2 len=3: status");
    TEST_ASSERT_EQ(0x5U, extracted, "spec example 11010110 start=2 len=3 -> 101b");

    /* Byte-wise extraction from a known word, sanity-checks alignment */
    {
        uint32_t word = 0x78563412U;
        status = bitUtilsExtractBits(word, 0, 8, &extracted);
        TEST_ASSERT_STATUS(BITUTILS_SUCCESS, status, "extract low byte: status");
        TEST_ASSERT_EQ(0x12U, extracted, "extract low byte (bits 0-7)");

        status = bitUtilsExtractBits(word, 24, 8, &extracted);
        TEST_ASSERT_STATUS(BITUTILS_SUCCESS, status, "extract high byte: status");
        TEST_ASSERT_EQ(0x78U, extracted, "extract high byte (bits 24-31)");
    }

    status = bitUtilsExtractBits(0x00000001U, 0, 1, &extracted);
    TEST_ASSERT_STATUS(BITUTILS_SUCCESS, status, "extract bit 0: status");
    TEST_ASSERT_EQ(0x1U, extracted, "extract single bit 0");

    status = bitUtilsExtractBits(0x80000000U, 31, 1, &extracted);
    TEST_ASSERT_STATUS(BITUTILS_SUCCESS, status, "extract bit 31: status");
    TEST_ASSERT_EQ(0x1U, extracted, "extract single bit 31 (MSB)");

    status = bitUtilsExtractBits(0x89ABCDEFU, 0, 32, &extracted);
    TEST_ASSERT_STATUS(BITUTILS_SUCCESS, status, "extract entire word: status");
    TEST_ASSERT_EQ(0x89ABCDEFU, extracted, "extract entire word (numBits == 32, startBit == 0)");

    extracted = 0xDEADBEEFU;
    status = bitUtilsExtractBits(0x00000000U, 32, 1, &extracted);
    TEST_ASSERT_STATUS(BITUTILS_ERROR_INVALID_START_BIT, status, "invalid start bit (32): status");
    TEST_ASSERT_EQ(0xDEADBEEFU, extracted, "invalid start bit: output untouched");

    extracted = 0xDEADBEEFU;
    status = bitUtilsExtractBits(0x00000000U, 0, 0, &extracted);
    TEST_ASSERT_STATUS(BITUTILS_ERROR_INVALID_NUM_BITS, status, "numBits == 0: status");
    TEST_ASSERT_EQ(0xDEADBEEFU, extracted, "numBits == 0: output untouched");

    status = bitUtilsExtractBits(0x00000000U, 30, 5, &extracted);
    TEST_ASSERT_STATUS(BITUTILS_ERROR_INVALID_NUM_BITS, status, "startBit + numBits > 32 (30+5): status");

    status = bitUtilsExtractBits(0x00000000U, 0, 8, NULL);
    TEST_ASSERT_STATUS(BITUTILS_ERROR_NULL_POINTER, status, "NULL output pointer");
}

/* ------------------------------------------------------------------------
 * 6. bitUtilsInsertBits
 * ------------------------------------------------------------------------ */
static void testInsertBits(void)
{
    uint32_t out = 0;
    BitUtilsStatus_t status;

    beginSection("bitUtilsInsertBits:");

    /* Spec example: dest=00000000, insert=111b, start=4 -> expected 01110000b (0x70) */
    status = bitUtilsInsertBits(0x00000000U, 0x7U, 4, 3, &out);
    TEST_ASSERT_STATUS(BITUTILS_SUCCESS, status, "spec example insert 111 at bit 4: status");
    TEST_ASSERT_EQ(0x70U, out, "spec example insert 111 at bit 4 -> 01110000b");

    status = bitUtilsInsertBits(0x00000000U, 0x1U, 0, 1, &out);
    TEST_ASSERT_STATUS(BITUTILS_SUCCESS, status, "insert at bit 0: status");
    TEST_ASSERT_EQ(0x00000001U, out, "insert at bit 0");

    status = bitUtilsInsertBits(0x00000000U, 0x1U, 31, 1, &out);
    TEST_ASSERT_STATUS(BITUTILS_SUCCESS, status, "insert at bit 31 (MSB): status");
    TEST_ASSERT_EQ(0x80000000U, out, "insert at bit 31 (MSB)");

    status = bitUtilsInsertBits(0x00000000U, 0x89ABCDEFU, 0, 32, &out);
    TEST_ASSERT_STATUS(BITUTILS_SUCCESS, status, "insert entire word: status");
    TEST_ASSERT_EQ(0x89ABCDEFU, out, "insert entire word (numBits == 32)");

    /* Insert must only touch the target field, not adjacent bits */
    status = bitUtilsInsertBits(0xFFFFFFFFU, 0x0U, 8, 8, &out);
    TEST_ASSERT_STATUS(BITUTILS_SUCCESS, status, "insert zeros into middle of all-ones: status");
    TEST_ASSERT_EQ(0xFFFF00FFU, out, "insert clears only the target field, leaves surrounding bits alone");

    out = 0xDEADBEEFU;
    status = bitUtilsInsertBits(0x00000000U, 0x0U, 32, 1, &out);
    TEST_ASSERT_STATUS(BITUTILS_ERROR_INVALID_START_BIT, status, "invalid start bit (32): status");
    TEST_ASSERT_EQ(0xDEADBEEFU, out, "invalid start bit: output untouched");

    status = bitUtilsInsertBits(0x00000000U, 0x0U, 30, 5, &out);
    TEST_ASSERT_STATUS(BITUTILS_ERROR_INVALID_NUM_BITS, status, "invalid width, startBit + numBits > 32 (30+5): status");

    status = bitUtilsInsertBits(0x00000000U, 0x0U, 0, 0, &out);
    TEST_ASSERT_STATUS(BITUTILS_ERROR_INVALID_NUM_BITS, status, "numBits == 0: status");

    /* bitsToInsert too large for the requested width: 3-bit field can hold 0-7, 8 overflows it */
    out = 0xDEADBEEFU;
    status = bitUtilsInsertBits(0x00000000U, 0x8U, 4, 3, &out);
    TEST_ASSERT_STATUS(BITUTILS_ERROR_INVALID_VALUE, status, "bitsToInsert (8) too large for 3-bit field: status");
    TEST_ASSERT_EQ(0xDEADBEEFU, out, "bitsToInsert too large: output untouched");

    status = bitUtilsInsertBits(0x00000000U, 0x0U, 0, 8, NULL);
    TEST_ASSERT_STATUS(BITUTILS_ERROR_NULL_POINTER, status, "NULL output pointer");
}

/* ------------------------------------------------------------------------
 * Round-trip: insert then extract the same field back out
 * ------------------------------------------------------------------------ */
static void testRoundTrip(void)
{
    uint32_t inserted = 0;
    uint32_t extracted = 0;
    BitUtilsStatus_t status;

    beginSection("round-trip (insert then extract):");

    status = bitUtilsInsertBits(0x12345678U, 0xABU, 12, 8, &inserted);
    TEST_ASSERT_STATUS(BITUTILS_SUCCESS, status, "insert 0xAB at bits 12-19: status");

    status = bitUtilsExtractBits(inserted, 12, 8, &extracted);
    TEST_ASSERT_STATUS(BITUTILS_SUCCESS, status, "extract bits 12-19 back out: status");
    TEST_ASSERT_EQ(0xABU, extracted, "round-trip: extracted value matches what was inserted");
}

int main(void)
{
    printf("Running bitUtils tests...\n");

    testSetBit();
    testClearBit();
    testToggleBit();
    testIsBitSet();
    testExtractBits();
    testInsertBits();
    testRoundTrip();

    printf("\n%d / %d tests passed.\n", passedTests, totalTests);

    return (passedTests == totalTests) ? 0 : 1;
}

/**************************************** END OF testBitUtils.c ****************************************/