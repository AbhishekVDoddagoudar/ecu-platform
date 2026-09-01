/******************************************************************************
 * @file   loggerCompileTimeTest.c
 * @brief  Compile-time filtering tests for the Logger module (logger.h).
 *
 * @details
 * This file specifically tests what loggerTest.c cannot: the behavior of
 * the LOGGER_ERROR/WARN/INFO/DEBUG macros themselves, which differs
 * depending on the LOGGER_COMPILE_TIME_LEVEL this file is built with.
 * This is NOT meaningful as a single build -- it must be compiled
 * multiple times, once per LOGGER_COMPILE_TIME_LEVEL value under test,
 * with the specific assertions that apply to that build's configuration.
 *
 * Build and run at every level, e.g.:
 *
 *      gcc -std=c99 -DLOGGER_COMPILE_TIME_LEVEL=LOGGER_LEVEL_OFF_VAL   ...
 *      gcc -std=c99 -DLOGGER_COMPILE_TIME_LEVEL=LOGGER_LEVEL_ERROR_VAL ...
 *      gcc -std=c99 -DLOGGER_COMPILE_TIME_LEVEL=LOGGER_LEVEL_WARN_VAL  ...
 *      gcc -std=c99 -DLOGGER_COMPILE_TIME_LEVEL=LOGGER_LEVEL_INFO_VAL  ...
 *      gcc -std=c99 -DLOGGER_COMPILE_TIME_LEVEL=LOGGER_LEVEL_DEBUG_VAL ...
 *
 * The single most important thing this file proves -- the reason it
 * exists as a separate file at all -- is that a compiled-out call's
 * arguments are genuinely never evaluated, not just that the call
 * produces no output. A side-effecting expression (expensiveFunction()
 * below) is used specifically because "no output" and "not evaluated"
 * are two different claims, and only the second one is what
 * docs/logger-design.md, Section 5 actually locks down.
 *
 * @author Abhishek Doddagoudar
 * @date   August 2026
 ******************************************************************************/

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <logger.h>

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

static char capturedMessage[LOGGER_MAX_MESSAGE_LENGTH * 2];
static size_t capturedLength;
static int backendCallCount;

static void resetCapture(void)
{
    memset(capturedMessage, 0, sizeof(capturedMessage));
    capturedLength = 0U;
    backendCallCount = 0;
}

static void captureBackend(const char *message, size_t length, void *context)
{
    (void)context;
    backendCallCount++;
    if (length < sizeof(capturedMessage))
    {
        memcpy(capturedMessage, message, length);
        capturedMessage[length] = '\0';
    }
    capturedLength = length;
}

/* Side-effecting "expensive" call, used to prove non-evaluation. */
static int sideEffectCount;
static int expensiveFunction(void)
{
    sideEffectCount++;
    return 123;
}

static void resetSideEffect(void)
{
    sideEffectCount = 0;
}

int main(void)
{
    Logger_t logger;

    printf("Running logger compile-time filter tests (LOGGER_COMPILE_TIME_LEVEL=%d)...\n",
           LOGGER_COMPILE_TIME_LEVEL);

    loggerInit(&logger, LOGGER_LEVEL_DEBUG); /* runtime threshold wide open --
                                                 only the compile-time gate is
                                                 under test in this file */
    loggerSetBackend(&logger, captureBackend, NULL);

    /* ---------------------------------------------------------------
     * The core guarantee: a compiled-out call's arguments are never
     * evaluated, for every severity that's actually excluded at this
     * build's LOGGER_COMPILE_TIME_LEVEL.
     * ------------------------------------------------------------- */

#if LOGGER_COMPILE_TIME_LEVEL < LOGGER_LEVEL_DEBUG_VAL
    resetSideEffect();
    resetCapture();
    LOGGER_DEBUG(&logger, "TEST", "value=%d", expensiveFunction());
    TEST_ASSERT(sideEffectCount == 0, "LOGGER_DEBUG's argument is NOT evaluated when DEBUG is compiled out");
    TEST_ASSERT(backendCallCount == 0, "LOGGER_DEBUG produces no backend call when DEBUG is compiled out");
#else
    resetSideEffect();
    resetCapture();
    LOGGER_DEBUG(&logger, "TEST", "value=%d", expensiveFunction());
    TEST_ASSERT(sideEffectCount == 1, "LOGGER_DEBUG's argument IS evaluated exactly once when DEBUG is compiled in");
    TEST_ASSERT(backendCallCount == 1, "LOGGER_DEBUG reaches the backend when DEBUG is compiled in (runtime threshold is DEBUG)");
    TEST_ASSERT(strcmp(capturedMessage, "[DEBUG][TEST] value=123") == 0, "compiled-in DEBUG call produces the correct message");
#endif

#if LOGGER_COMPILE_TIME_LEVEL < LOGGER_LEVEL_INFO_VAL
    resetSideEffect();
    resetCapture();
    LOGGER_INFO(&logger, "TEST", "value=%d", expensiveFunction());
    TEST_ASSERT(sideEffectCount == 0, "LOGGER_INFO's argument is NOT evaluated when INFO is compiled out");
    TEST_ASSERT(backendCallCount == 0, "LOGGER_INFO produces no backend call when INFO is compiled out");
#else
    resetSideEffect();
    resetCapture();
    LOGGER_INFO(&logger, "TEST", "value=%d", expensiveFunction());
    TEST_ASSERT(sideEffectCount == 1, "LOGGER_INFO's argument IS evaluated exactly once when INFO is compiled in");
    TEST_ASSERT(backendCallCount == 1, "LOGGER_INFO reaches the backend when INFO is compiled in");
#endif

#if LOGGER_COMPILE_TIME_LEVEL < LOGGER_LEVEL_WARN_VAL
    resetSideEffect();
    resetCapture();
    LOGGER_WARN(&logger, "TEST", "value=%d", expensiveFunction());
    TEST_ASSERT(sideEffectCount == 0, "LOGGER_WARN's argument is NOT evaluated when WARN is compiled out");
    TEST_ASSERT(backendCallCount == 0, "LOGGER_WARN produces no backend call when WARN is compiled out");
#else
    resetSideEffect();
    resetCapture();
    LOGGER_WARN(&logger, "TEST", "value=%d", expensiveFunction());
    TEST_ASSERT(sideEffectCount == 1, "LOGGER_WARN's argument IS evaluated exactly once when WARN is compiled in");
    TEST_ASSERT(backendCallCount == 1, "LOGGER_WARN reaches the backend when WARN is compiled in");
#endif

#if LOGGER_COMPILE_TIME_LEVEL < LOGGER_LEVEL_ERROR_VAL
    resetSideEffect();
    resetCapture();
    LOGGER_ERROR(&logger, "TEST", "value=%d", expensiveFunction());
    TEST_ASSERT(sideEffectCount == 0, "LOGGER_ERROR's argument is NOT evaluated when ERROR is compiled out (LOGGER_COMPILE_TIME_LEVEL == OFF)");
    TEST_ASSERT(backendCallCount == 0, "LOGGER_ERROR produces no backend call when ERROR is compiled out");
#else
    resetSideEffect();
    resetCapture();
    LOGGER_ERROR(&logger, "TEST", "value=%d", expensiveFunction());
    TEST_ASSERT(sideEffectCount == 1, "LOGGER_ERROR's argument IS evaluated exactly once when ERROR is compiled in");
    TEST_ASSERT(backendCallCount == 1, "LOGGER_ERROR reaches the backend when ERROR is compiled in");
#endif

    /* ---------------------------------------------------------------
     * All four macros must compile cleanly as no-ops when
     * LOGGER_COMPILE_TIME_LEVEL == OFF -- specifically confirms the
     * macros parse and produce valid, warning-free statements at the
     * most restrictive setting, not just that arguments aren't
     * evaluated at less-restrictive settings above.
     * ------------------------------------------------------------- */
#if LOGGER_COMPILE_TIME_LEVEL == LOGGER_LEVEL_OFF_VAL
    resetSideEffect();
    resetCapture();
    LOGGER_ERROR(&logger, "TEST", "a");
    LOGGER_WARN(&logger, "TEST", "b");
    LOGGER_INFO(&logger, "TEST", "c");
    LOGGER_DEBUG(&logger, "TEST", "d");
    TEST_ASSERT(backendCallCount == 0, "at LOGGER_COMPILE_TIME_LEVEL == OFF, all four macros are no-ops with zero backend calls");
#endif

    /* ---------------------------------------------------------------
     * loggerWrite() itself is unaffected by LOGGER_COMPILE_TIME_LEVEL --
     * it's a real function, always present, and always evaluates its
     * arguments and performs its own runtime check regardless of what
     * this build excluded at the macro layer.
     * ------------------------------------------------------------- */
    resetSideEffect();
    resetCapture();
    loggerWrite(&logger, LOGGER_LEVEL_DEBUG, "TEST", "direct=%d", expensiveFunction());
    TEST_ASSERT(sideEffectCount == 1, "calling loggerWrite() directly always evaluates its arguments, regardless of LOGGER_COMPILE_TIME_LEVEL");
    TEST_ASSERT(backendCallCount == 1, "calling loggerWrite() directly always reaches the backend here, regardless of LOGGER_COMPILE_TIME_LEVEL");

    printf("\n%d / %d tests passed (LOGGER_COMPILE_TIME_LEVEL=%d).\n", passedTests, totalTests, LOGGER_COMPILE_TIME_LEVEL);

    return (passedTests == totalTests) ? 0 : 1;
}

/**************************************** END OF loggerCompileTimeTest.c ****************************************/