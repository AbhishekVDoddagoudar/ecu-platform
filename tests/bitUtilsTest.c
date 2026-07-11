/******************************************************************************
 * @file   bitUtilsTest.c
 * @brief  Unit tests for bit manipulation utilities (bitUtils.h).
 *
 * @author Abhishek Doddagoudar
 *
 * @date   July 2026
 ******************************************************************************/

#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <bitUtils.h>

static int totalTests = 0;
static int passedTests = 0;

/**
 * @brief Run callback in a child process and verify it aborts via assert().
 *
 * bitUtils.c validates its preconditions with assert(), which calls abort()
 * (raising SIGABRT) rather than returning an error code. The only way to
 * test that invalid input is actually rejected is to run the risky call in
 * an isolated child process and check that it was killed by SIGABRT -- if
 * we called it directly in this process, a failed assert would kill the
 * entire test binary and no further tests would run.
 *
 * NOTE: this only works in debug builds. If NDEBUG is defined, assert()
 * compiles to nothing and these calls will return normally instead of
 * aborting -- do not build these tests with -DNDEBUG.
 */
typedef void (*RiskyCall)(void);

static int expectAssertFailure(RiskyCall callback, const char *desc)
{
    fflush(stdout);
    pid_t pid = fork();

    if (pid == 0)
    {
        /* Child: silence the assert() diagnostic so test output stays clean,
         * then make the call we expect to abort. */
        freopen("/dev/null", "w", stderr);
        callback();
        _exit(0); /* Only reached if the assert did NOT fire -- that's a failure. */
    }

    int status = 0;
    waitpid(pid, &status, 0);
    int aborted = WIFSIGNALED(status) && (WTERMSIG(status) == SIGABRT);

    totalTests++;
    if (aborted)
    {
        passedTests++;
    }
    else
    {
        printf("  FAIL: %s (expected assert()/SIGABRT, but call returned normally)\n", desc);
    }

    return aborted;
}

#define TEST_ASSERT(cond, desc)                               \
    do                                                        \
    {                                                         \
        totalTests++;                                         \
        if (cond)                                             \
        {                                                     \
            passedTests++;                                    \
        }                                                     \
        else                                                  \
        {                                                     \
            printf("  FAIL (line %d): %s\n", __LINE__, desc); \
        }                                                     \
    } while (0)

static void testSetBit(void)
{
    printf("bitUtilsSetBit:\n");

    TEST_ASSERT(bitUtilsSetBit(0x00000000U, 0) == 0x00000001U, "set bit 0 on zero");
    TEST_ASSERT(bitUtilsSetBit(0x00000000U, 31) == 0x80000000U, "set bit 31 (MSB) on zero");
    TEST_ASSERT(bitUtilsSetBit(0x0000000AU, 2) == 0x0000000EU, "set a clear bit among others (0xA -> 0xE)");
    TEST_ASSERT(bitUtilsSetBit(0x0000000AU, 1) == 0x0000000AU, "setting an already-set bit is a no-op");
    TEST_ASSERT(bitUtilsSetBit(0xFFFFFFFFU, 15) == 0xFFFFFFFFU, "set bit on all-ones value stays all-ones");
}

static void testClearBit(void)
{
    printf("bitUtilsClearBit:\n");

    TEST_ASSERT(bitUtilsClearBit(0xFFFFFFFFU, 0) == 0xFFFFFFFEU, "clear bit 0 on all-ones");
    TEST_ASSERT(bitUtilsClearBit(0xFFFFFFFFU, 31) == 0x7FFFFFFFU, "clear bit 31 (MSB) on all-ones");
    TEST_ASSERT(bitUtilsClearBit(0x0000000AU, 1) == 0x00000008U, "clear a set bit among others (0xA -> 0x8)");
    TEST_ASSERT(bitUtilsClearBit(0x0000000AU, 0) == 0x0000000AU, "clearing an already-clear bit is a no-op");
    TEST_ASSERT(bitUtilsClearBit(0x00000000U, 5) == 0x00000000U, "clear bit on zero stays zero");
}

static void testToggleBit(void)
{
    printf("bitUtilsToggleBit:\n");

    TEST_ASSERT(bitUtilsToggleBit(0x00000000U, 4) == 0x00000010U, "toggle clear bit -> set");
    TEST_ASSERT(bitUtilsToggleBit(0x00000010U, 4) == 0x00000000U, "toggle set bit -> clear");
    {
        uint32_t v = 0x0000000AU;
        uint32_t once = bitUtilsToggleBit(v, 2);
        uint32_t twice = bitUtilsToggleBit(once, 2);
        TEST_ASSERT(twice == v, "double toggle restores original value (0xA, bit 2)");
    }
    TEST_ASSERT(bitUtilsToggleBit(0x00000000U, 31) == 0x80000000U, "toggle bit 31 (MSB) on zero");
}

static void testIsBitSet(void)
{
    printf("bitUtilsIsBitSet:\n");

    TEST_ASSERT(bitUtilsIsBitSet(0x0000000AU, 1) == true, "bit 1 set in 0xA");
    TEST_ASSERT(bitUtilsIsBitSet(0x0000000AU, 0) == false, "bit 0 clear in 0xA");
    TEST_ASSERT(bitUtilsIsBitSet(0x80000000U, 31) == true, "MSB (bit 31) detected as set");
    TEST_ASSERT(bitUtilsIsBitSet(0x00000000U, 15) == false, "no bits set in zero");
    TEST_ASSERT(bitUtilsIsBitSet(0xFFFFFFFFU, 20) == true, "any bit set in all-ones value");
}

static void testExtractBits(void)
{
    printf("bitUtilsExtractBits:\n");

    /* 0x78563412 -> binary bytes: 78 56 34 12 (value as a 32-bit word) */
    uint32_t word = 0x78563412U;

    TEST_ASSERT(bitUtilsExtractBits(word, 0, 8) == 0x12U, "extract low byte");
    TEST_ASSERT(bitUtilsExtractBits(word, 8, 8) == 0x34U, "extract second byte");
    TEST_ASSERT(bitUtilsExtractBits(word, 16, 8) == 0x56U, "extract third byte");
    TEST_ASSERT(bitUtilsExtractBits(word, 24, 8) == 0x78U, "extract high byte");
    TEST_ASSERT(bitUtilsExtractBits(word, 0, 32) == word, "extract all 32 bits returns original value");
    TEST_ASSERT(bitUtilsExtractBits(0xFFFFFFFFU, 31, 1) == 0x1U, "extract single MSB from all-ones");
    TEST_ASSERT(bitUtilsExtractBits(0x0000000EU, 1, 3) == 0x7U, "extract mid-range bits (0xE, bits 1-3 -> 0x7)");
}

static void testInsertBits(void)
{
    printf("bitUtilsInsertBits:\n");

    TEST_ASSERT(bitUtilsInsertBits(0x00000000U, 0xFFU, 8, 8) == 0x0000FF00U,
                "insert a full byte into a zero value");
    TEST_ASSERT(bitUtilsInsertBits(0xFFFFFFFFU, 0x0U, 8, 8) == 0xFFFF00FFU,
                "insert zeros into an all-ones value clears just that byte");
    TEST_ASSERT(bitUtilsInsertBits(0x00000000U, 0xFFFFFFFFU, 0, 32) == 0xFFFFFFFFU,
                "insert all 32 bits overwrites the entire value");
    TEST_ASSERT(bitUtilsInsertBits(0x000000FFU, 0x1U, 0, 1) == 0x000000FFU,
                "inserting a bit that's already that value is a no-op");
    {
        /* bitsToInsert wider than numBits must be masked down before insertion */
        uint32_t result = bitUtilsInsertBits(0x00000000U, 0xFFU, 0, 4);
        TEST_ASSERT(result == 0x0000000FU,
                    "extra high bits in bitsToInsert beyond numBits are masked off, not inserted");
    }
    TEST_ASSERT(bitUtilsInsertBits(0x0000000AU, 0x3U, 0, 2) == 0x0000000BU,
                "insert low 2 bits into value with other bits already set (0xA -> 0xB)");
}

/* --- Negative-case wrappers: each of these is expected to trip an assert() --- */
static void callSetBitOutOfRange(void) { (void)bitUtilsSetBit(0, 32); }
static void callSetBitWayOutOfRange(void) { (void)bitUtilsSetBit(0, 255); }
static void callClearBitOutOfRange(void) { (void)bitUtilsClearBit(0, 32); }
static void callToggleBitOutOfRange(void) { (void)bitUtilsToggleBit(0, 32); }
static void callIsBitSetOutOfRange(void) { (void)bitUtilsIsBitSet(0, 32); }
static void callExtractBitsStartOutOfRange(void) { (void)bitUtilsExtractBits(0, 32, 1); }
static void callExtractBitsNumBitsZero(void) { (void)bitUtilsExtractBits(0, 0, 0); }
static void callExtractBitsNumBitsTooLarge(void) { (void)bitUtilsExtractBits(0, 30, 5); } /* 30+5 > 32 */
static void callInsertBitsStartOutOfRange(void) { (void)bitUtilsInsertBits(0, 0, 32, 1); }
static void callInsertBitsNumBitsZero(void) { (void)bitUtilsInsertBits(0, 0, 0, 0); }
static void callInsertBitsNumBitsTooLarge(void) { (void)bitUtilsInsertBits(0, 0, 30, 5); } /* 30+5 > 32 */

static void testInvalidBitPositionAsserts(void)
{
    printf("negative: bitPosition >= 32 triggers assert() (SetBit/ClearBit/ToggleBit/IsBitSet):\n");

    expectAssertFailure(callSetBitOutOfRange, "bitUtilsSetBit(0, 32) should assert (32 is out of range)");
    expectAssertFailure(callSetBitWayOutOfRange, "bitUtilsSetBit(0, 255) should assert (255 is way out of range)");
    expectAssertFailure(callClearBitOutOfRange, "bitUtilsClearBit(0, 32) should assert");
    expectAssertFailure(callToggleBitOutOfRange, "bitUtilsToggleBit(0, 32) should assert");
    expectAssertFailure(callIsBitSetOutOfRange, "bitUtilsIsBitSet(0, 32) should assert");
}

static void testInvalidExtractBitsAsserts(void)
{
    printf("negative: bitUtilsExtractBits invalid startBit/numBits triggers assert():\n");

    expectAssertFailure(callExtractBitsStartOutOfRange, "startBit == 32 should assert (must be < 32)");
    expectAssertFailure(callExtractBitsNumBitsZero, "numBits == 0 should assert (must extract at least 1 bit)");
    expectAssertFailure(callExtractBitsNumBitsTooLarge, "startBit + numBits > 32 should assert (reads past the word)");
}

static void testInvalidInsertBitsAsserts(void)
{
    printf("negative: bitUtilsInsertBits invalid startBit/numBits triggers assert():\n");

    expectAssertFailure(callInsertBitsStartOutOfRange, "startBit == 32 should assert (must be < 32)");
    expectAssertFailure(callInsertBitsNumBitsZero, "numBits == 0 should assert (must insert at least 1 bit)");
    expectAssertFailure(callInsertBitsNumBitsTooLarge, "startBit + numBits > 32 should assert (writes past the word)");
}

static void testRoundTrip(void)
{
    printf("round-trip (insert then extract):\n");

    uint32_t base = 0x00000000U;
    uint32_t withField = bitUtilsInsertBits(base, 0x1AU, 5, 6);
    uint32_t extracted = bitUtilsExtractBits(withField, 5, 6);

    TEST_ASSERT(extracted == 0x1AU, "value inserted at an offset extracts back out unchanged");
}

int main(void)
{
    printf("Running bitUtils tests...\n\n");

    testSetBit();
    testClearBit();
    testToggleBit();
    testIsBitSet();
    testExtractBits();
    testInsertBits();
    testInvalidBitPositionAsserts();
    testInvalidExtractBitsAsserts();
    testInvalidInsertBitsAsserts();
    testRoundTrip();

    printf("\n%d / %d tests passed.\n", passedTests, totalTests);

    return (passedTests == totalTests) ? 0 : 1;
}

/**************************************** END OF bitUtilsTest.c ****************************************/