/******************************************************************************
 * @file   crc16Test.c
 * @brief  Unit tests for CRC-16/CCITT-FALSE checksum calculation (crc16.h).
 *
 * @author Abhishek Doddagoudar
 *
 * @date   July 2026
 ******************************************************************************/

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <crc16.h>

static int totalTests = 0;
static int passedTests = 0;

#define TEST_ASSERT(cond, desc)                               \
    do                                                        \
    {                                                         \
        totalTests++;                                         \
        if (cond)                                             \
        {                                                     \
            printf("  PASS (line %d): %s\n", __LINE__, desc); \
            passedTests++;                                    \
        }                                                     \
        else                                                  \
        {                                                     \
            printf("  FAIL (line %d): %s\n", __LINE__, desc); \
        }                                                     \
    } while (0)

static void testKnownVector(void)
{
    printf("crc16Calculate known-answer test:\n");

    /* Standard CRC-16/CCITT-FALSE check value for ASCII "123456789" is 0x29B1.
     * This is the canonical test vector used to confirm polynomial 0x1021,
     * initial value 0xFFFF, and non-reflected in/out are all correctly implemented. */
    const uint8_t testData[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    uint16_t result = crc16Calculate(testData, sizeof(testData));

    TEST_ASSERT(result == 0x29B1U, "CRC of \"123456789\" matches CCITT-FALSE check value 0x29B1");
}

static void testEmptyBuffer(void)
{
    printf("crc16Calculate empty buffer:\n");

    uint16_t result = crc16Calculate((const uint8_t *)"", 0);

    TEST_ASSERT(result == CRC16_INITIAL_VALUE, "CRC of a zero-length buffer equals the initial value (0xFFFF)");
}

static void testNullPointer(void)
{
    printf("crc16Calculate / crc16Update NULL handling:\n");

    TEST_ASSERT(crc16Calculate(NULL, 5) == CRC16_INITIAL_VALUE,
                "crc16Calculate with NULL data returns the initial value rather than crashing");

    uint16_t running = 0x1234U;
    TEST_ASSERT(crc16Update(running, NULL, 5) == running,
                "crc16Update with NULL data returns currentCRC unchanged rather than crashing");

    TEST_ASSERT(crc16Update(running, (const uint8_t *)"x", 0) == running,
                "crc16Update with length 0 returns currentCRC unchanged");
}

static void testSingleByteVsKnownBehavior(void)
{
    printf("crc16Calculate single-byte sanity:\n");

    uint8_t singleByte[] = {0x00};
    uint16_t result = crc16Calculate(singleByte, 1);

    /* Not an external spec value, but the crc must differ from the initial
     * value after processing at least one byte -- this catches an
     * accidentally-empty loop body or a return-before-processing bug. */
    TEST_ASSERT(result != CRC16_INITIAL_VALUE,
                "CRC changes from the initial value after processing one byte");
}

static void testUpdateMatchesCalculateWholeBuffer(void)
{
    printf("crc16Update seeded with the initial value matches crc16Calculate:\n");

    const uint8_t testData[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};

    uint16_t viaCalculate = crc16Calculate(testData, sizeof(testData));
    uint16_t viaUpdate = crc16Update(CRC16_INITIAL_VALUE, testData, sizeof(testData));

    TEST_ASSERT(viaCalculate == viaUpdate,
                "crc16Update(CRC16_INITIAL_VALUE, buf, len) equals crc16Calculate(buf, len)");
}

static void testIncrementalChunking(void)
{
    printf("crc16Update incremental chunking:\n");

    const uint8_t testData[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    size_t total = sizeof(testData);

    /* Feed the same data in two chunks and confirm it matches processing it whole. */
    uint16_t whole = crc16Calculate(testData, total);

    uint16_t chunked = crc16Update(CRC16_INITIAL_VALUE, testData, 4);
    chunked = crc16Update(chunked, testData + 4, total - 4);

    TEST_ASSERT(chunked == whole,
                "splitting input across two crc16Update calls gives the same result as one crc16Calculate call");

    /* Also verify with a different, uneven split (single-byte chunks). */
    uint16_t byteByByte = CRC16_INITIAL_VALUE;
    for (size_t i = 0; i < total; i++)
    {
        byteByByte = crc16Update(byteByByte, &testData[i], 1);
    }

    TEST_ASSERT(byteByByte == whole,
                "feeding data one byte at a time via crc16Update gives the same result as crc16Calculate");
}

static void testCorruptedKnownVectorDoesNotMatch(void)
{
    printf("negative: corrupted \"123456789\" must NOT match the known-good CRC:\n");

    /* Same known vector as testKnownVector, but with the last character
     * flipped from '9' to '0'. A correct CRC implementation must produce a
     * different value here -- if it doesn't, the CRC is failing to detect
     * corruption, which defeats its entire purpose. */
    const uint8_t corrupted[] = {'1', '2', '3', '4', '5', '6', '7', '8', '0'};
    uint16_t result = crc16Calculate(corrupted, sizeof(corrupted));

    TEST_ASSERT(result != 0x29B1U, "corrupting one character of the known vector changes the CRC");
}

static void testTruncatedBufferDoesNotMatch(void)
{
    printf("negative: truncated buffer must NOT match the full-buffer CRC:\n");

    const uint8_t testData[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    uint16_t fullCrc = crc16Calculate(testData, sizeof(testData));
    uint16_t truncatedCrc = crc16Calculate(testData, sizeof(testData) - 1); /* drop the last byte */

    TEST_ASSERT(fullCrc != truncatedCrc, "dropping the last byte changes the CRC (length matters, not just content)");
}

static void testAppendedByteDoesNotMatch(void)
{
    printf("negative: appending an extra byte must NOT match the original CRC:\n");

    const uint8_t testData[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '\0'}; /* trailing NUL appended */
    uint16_t originalCrc = crc16Calculate(testData, sizeof(testData) - 1);
    uint16_t extendedCrc = crc16Calculate(testData, sizeof(testData));

    TEST_ASSERT(originalCrc != extendedCrc, "appending a trailing byte (even 0x00) changes the CRC");
}

static void testDifferentDataDifferentCrc(void)
{
    printf("crc16Calculate distinguishes different inputs:\n");

    const uint8_t dataA[] = {0x01, 0x02, 0x03};
    const uint8_t dataB[] = {0x01, 0x02, 0x04}; /* last byte differs */

    uint16_t crcA = crc16Calculate(dataA, sizeof(dataA));
    uint16_t crcB = crc16Calculate(dataB, sizeof(dataB));

    TEST_ASSERT(crcA != crcB, "a single differing byte produces a different CRC");
}

int main(void)
{
    printf("Running crc16 tests...\n\n");

    testKnownVector();
    testEmptyBuffer();
    testNullPointer();
    testSingleByteVsKnownBehavior();
    testUpdateMatchesCalculateWholeBuffer();
    testIncrementalChunking();
    testCorruptedKnownVectorDoesNotMatch();
    testTruncatedBufferDoesNotMatch();
    testAppendedByteDoesNotMatch();
    testDifferentDataDifferentCrc();

    printf("\n%d / %d tests passed.\n", passedTests, totalTests);

    return (passedTests == totalTests) ? 0 : 1;
}

/**************************************** END OF crc16Test.c ****************************************/