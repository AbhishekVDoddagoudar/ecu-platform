/******************************************************************************
 * @file   loggerTest.c
 * @brief  Runtime unit tests for the Logger module (logger.h).
 *
 * @details
 * This file covers everything about Logger that is observable regardless
 * of LOGGER_COMPILE_TIME_LEVEL: loggerInit/SetBackend/ClearBackend/
 * SetLevel/GetLevel and loggerWrite() called directly. It deliberately
 * does NOT test the LOGGER_ERROR/WARN/INFO/DEBUG macros' compile-time
 * argument-elimination behavior -- that requires building this test
 * binary (or a dedicated one) at specific LOGGER_COMPILE_TIME_LEVEL
 * values and is covered separately by loggerCompileTimeTest.c, per
 * review: "you need compile-time macro tests... that is the only
 * convincing way to prove your most important compile-time guarantee."
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

/* -------------------------------------------------------------------------
 * Shared backend instrumentation
 * ------------------------------------------------------------------------- */

static char capturedMessage[LOGGER_MAX_MESSAGE_LENGTH * 2];
static size_t capturedLength;
static int backendCallCount;
static void *capturedContext;

static void resetCapture(void)
{
    memset(capturedMessage, 0, sizeof(capturedMessage));
    capturedLength = 0U;
    backendCallCount = 0;
    capturedContext = NULL;
}

static void captureBackend(const char *message, size_t length, void *context)
{
    backendCallCount++;
    capturedContext = context;
    if (length < sizeof(capturedMessage))
    {
        memcpy(capturedMessage, message, length);
        capturedMessage[length] = '\0';
    }
    capturedLength = length;
}

/* Second, independent backend/buffer pair, used for reentrancy/
 * independence tests where two loggers must not interfere. */
static char capturedMessageB[LOGGER_MAX_MESSAGE_LENGTH * 2];
static size_t capturedLengthB;
static void captureBackendB(const char *message, size_t length, void *context)
{
    (void)context;
    if (length < sizeof(capturedMessageB))
    {
        memcpy(capturedMessageB, message, length);
        capturedMessageB[length] = '\0';
    }
    capturedLengthB = length;
}

/* -------------------------------------------------------------------------
 * loggerInit
 * ------------------------------------------------------------------------- */
static void testInitEveryValidThreshold(void)
{
    Logger_t logger;
    LoggerLevel_t validLevels[] = {LOGGER_LEVEL_OFF, LOGGER_LEVEL_ERROR, LOGGER_LEVEL_WARN,
                                    LOGGER_LEVEL_INFO, LOGGER_LEVEL_DEBUG};
    size_t i;

    printf("\nloggerInit with every valid threshold:\n");

    for (i = 0U; i < (sizeof(validLevels) / sizeof(validLevels[0])); i++)
    {
        LoggerStatus_t status = loggerInit(&logger, validLevels[i]);
        LoggerLevel_t readBack = (LoggerLevel_t)-1;

        TEST_ASSERT(status == LOGGER_SUCCESS, "loggerInit succeeds for a valid threshold");
        TEST_ASSERT(logger.backend == NULL, "loggerInit leaves backend NULL");
        TEST_ASSERT(logger.backendContext == NULL, "loggerInit leaves backendContext NULL");

        loggerGetLevel(&logger, &readBack);
        TEST_ASSERT(readBack == validLevels[i], "loggerInit sets the requested threshold exactly");
    }
}

static void testInitInvalidThresholdAndNullLogger(void)
{
    Logger_t logger;
    LoggerStatus_t status;

    printf("\nloggerInit invalid threshold / NULL logger:\n");

    status = loggerInit(NULL, LOGGER_LEVEL_INFO);
    TEST_ASSERT(status == LOGGER_ERROR_NULL_POINTER, "loggerInit(NULL, ...) reports NULL pointer");

    status = loggerInit(&logger, (LoggerLevel_t)99);
    TEST_ASSERT(status == LOGGER_ERROR_INVALID_LEVEL, "loggerInit rejects an out-of-range threshold");
}

/* -------------------------------------------------------------------------
 * loggerSetLevel / loggerGetLevel
 * ------------------------------------------------------------------------- */
static void testSetGetLevel(void)
{
    Logger_t logger;
    LoggerLevel_t readBack;
    LoggerStatus_t status;

    printf("\nloggerSetLevel / loggerGetLevel:\n");

    loggerInit(&logger, LOGGER_LEVEL_WARN);

    status = loggerSetLevel(&logger, LOGGER_LEVEL_DEBUG);
    TEST_ASSERT(status == LOGGER_SUCCESS, "loggerSetLevel succeeds for a valid level");
    loggerGetLevel(&logger, &readBack);
    TEST_ASSERT(readBack == LOGGER_LEVEL_DEBUG, "loggerGetLevel reflects the newly-set level");

    status = loggerSetLevel(&logger, (LoggerLevel_t)123);
    TEST_ASSERT(status == LOGGER_ERROR_INVALID_LEVEL, "loggerSetLevel rejects an invalid level");
    loggerGetLevel(&logger, &readBack);
    TEST_ASSERT(readBack == LOGGER_LEVEL_DEBUG, "a rejected loggerSetLevel leaves the previous threshold unchanged");

    status = loggerSetLevel(NULL, LOGGER_LEVEL_INFO);
    TEST_ASSERT(status == LOGGER_ERROR_NULL_POINTER, "loggerSetLevel(NULL, ...) reports NULL pointer");

    status = loggerGetLevel(NULL, &readBack);
    TEST_ASSERT(status == LOGGER_ERROR_NULL_POINTER, "loggerGetLevel(NULL, ...) reports NULL pointer");

    status = loggerGetLevel(&logger, NULL);
    TEST_ASSERT(status == LOGGER_ERROR_NULL_POINTER, "loggerGetLevel(logger, NULL) reports NULL pointer");
}

/* -------------------------------------------------------------------------
 * Backend registration / clearing / context passthrough
 * ------------------------------------------------------------------------- */
static void testBackendLifecycleAndContextPassthrough(void)
{
    Logger_t logger;
    LoggerStatus_t status;
    int contextValue = 42;

    printf("\nBackend registration / clearing / context passthrough:\n");

    loggerInit(&logger, LOGGER_LEVEL_DEBUG);

    status = loggerSetBackend(NULL, captureBackend, NULL);
    TEST_ASSERT(status == LOGGER_ERROR_NULL_POINTER, "loggerSetBackend(NULL, ...) reports NULL pointer");

    status = loggerSetBackend(&logger, NULL, NULL);
    TEST_ASSERT(status == LOGGER_ERROR_NULL_POINTER, "loggerSetBackend rejects a NULL backend function");

    status = loggerSetBackend(&logger, captureBackend, &contextValue);
    TEST_ASSERT(status == LOGGER_SUCCESS, "loggerSetBackend succeeds with a valid backend");
    TEST_ASSERT(logger.backend == captureBackend, "backend pointer stored exactly as given");
    TEST_ASSERT(logger.backendContext == &contextValue, "backendContext stored exactly as given");

    resetCapture();
    loggerWrite(&logger, LOGGER_LEVEL_ERROR, "MOD", "msg");
    TEST_ASSERT(capturedContext == &contextValue, "the registered backendContext is passed through to the backend call");

    status = loggerClearBackend(&logger);
    TEST_ASSERT(status == LOGGER_SUCCESS, "loggerClearBackend succeeds");
    TEST_ASSERT(logger.backend == NULL, "loggerClearBackend resets backend to NULL");
    TEST_ASSERT(logger.backendContext == NULL, "loggerClearBackend resets backendContext to NULL");

    status = loggerClearBackend(NULL);
    TEST_ASSERT(status == LOGGER_ERROR_NULL_POINTER, "loggerClearBackend(NULL) reports NULL pointer");
}

/* -------------------------------------------------------------------------
 * loggerWrite: NULL handling
 * ------------------------------------------------------------------------- */
static void testWriteNullLogger(void)
{
    LoggerStatus_t status;

    printf("\nloggerWrite NULL logger:\n");

    status = loggerWrite(NULL, LOGGER_LEVEL_ERROR, "MOD", "msg");
    TEST_ASSERT(status == LOGGER_ERROR_NULL_POINTER, "loggerWrite(NULL, ...) reports NULL pointer");
}

static void testWriteNullModuleAndFmtWhenAllowed(void)
{
    Logger_t logger;
    LoggerStatus_t status;

    printf("\nloggerWrite NULL module / NULL fmt, call NOT filtered (allowed):\n");

    loggerInit(&logger, LOGGER_LEVEL_DEBUG);
    loggerSetBackend(&logger, captureBackend, NULL);

    status = loggerWrite(&logger, LOGGER_LEVEL_ERROR, NULL, "msg");
    TEST_ASSERT(status == LOGGER_ERROR_NULL_POINTER, "allowed call with NULL module reports NULL pointer");

    status = loggerWrite(&logger, LOGGER_LEVEL_ERROR, "MOD", NULL);
    TEST_ASSERT(status == LOGGER_ERROR_NULL_POINTER, "allowed call with NULL fmt reports NULL pointer");
}

/* -------------------------------------------------------------------------
 * loggerWrite: invalid severity, including OFF
 * ------------------------------------------------------------------------- */
static void testWriteInvalidSeverity(void)
{
    Logger_t logger;
    LoggerStatus_t status;

    printf("\nloggerWrite invalid severity (including LOGGER_LEVEL_OFF used as a call's own level):\n");

    loggerInit(&logger, LOGGER_LEVEL_DEBUG);
    loggerSetBackend(&logger, captureBackend, NULL);

    status = loggerWrite(&logger, LOGGER_LEVEL_OFF, "MOD", "msg");
    TEST_ASSERT(status == LOGGER_ERROR_INVALID_LEVEL, "LOGGER_LEVEL_OFF is rejected as a call's own severity");

    status = loggerWrite(&logger, (LoggerLevel_t)77, "MOD", "msg");
    TEST_ASSERT(status == LOGGER_ERROR_INVALID_LEVEL, "an out-of-range level is rejected");
}

/* -------------------------------------------------------------------------
 * Runtime filtering at every threshold
 * ------------------------------------------------------------------------- */
static void testRuntimeFilteringAtEveryThreshold(void)
{
    Logger_t logger;
    LoggerLevel_t thresholds[] = {LOGGER_LEVEL_OFF, LOGGER_LEVEL_ERROR, LOGGER_LEVEL_WARN,
                                   LOGGER_LEVEL_INFO, LOGGER_LEVEL_DEBUG};
    LoggerLevel_t severities[] = {LOGGER_LEVEL_ERROR, LOGGER_LEVEL_WARN, LOGGER_LEVEL_INFO, LOGGER_LEVEL_DEBUG};
    size_t t;
    size_t s;

    printf("\nRuntime filtering at every threshold x every severity:\n");

    loggerInit(&logger, LOGGER_LEVEL_OFF);
    loggerSetBackend(&logger, captureBackend, NULL);

    for (t = 0U; t < (sizeof(thresholds) / sizeof(thresholds[0])); t++)
    {
        loggerSetLevel(&logger, thresholds[t]);

        for (s = 0U; s < (sizeof(severities) / sizeof(severities[0])); s++)
        {
            int expectEmitted = (severities[s] <= thresholds[t]) ? 1 : 0;

            resetCapture();
            loggerWrite(&logger, severities[s], "MOD", "msg");

            TEST_ASSERT(backendCallCount == expectEmitted,
                        expectEmitted ? "severity at/above threshold reaches the backend"
                                       : "severity below threshold does not reach the backend");
        }
    }
}

static void testLoggerLevelOffSuppressesEverything(void)
{
    Logger_t logger;

    printf("\nLOGGER_LEVEL_OFF as runtime threshold suppresses every real severity:\n");

    loggerInit(&logger, LOGGER_LEVEL_OFF);
    loggerSetBackend(&logger, captureBackend, NULL);

    resetCapture();
    loggerWrite(&logger, LOGGER_LEVEL_ERROR, "MOD", "should not emit");
    loggerWrite(&logger, LOGGER_LEVEL_DEBUG, "MOD", "should not emit either");

    TEST_ASSERT(backendCallCount == 0, "LOGGER_LEVEL_OFF suppresses ERROR through DEBUG, all of them");
}

/* -------------------------------------------------------------------------
 * Allowed message with no backend
 * ------------------------------------------------------------------------- */
static void testAllowedMessageNoBackend(void)
{
    Logger_t logger;
    LoggerStatus_t status;

    printf("\nAllowed (unfiltered) message with no backend registered:\n");

    loggerInit(&logger, LOGGER_LEVEL_DEBUG); /* backend == NULL after Init */

    status = loggerWrite(&logger, LOGGER_LEVEL_ERROR, "MOD", "msg");
    TEST_ASSERT(status == LOGGER_ERROR_NO_BACKEND, "an allowed call with no backend reports NO_BACKEND");
}

/* -------------------------------------------------------------------------
 * The filtered-call short-circuit: NULL module, NULL fmt, and no backend
 * all become irrelevant once a call is filtered
 * ------------------------------------------------------------------------- */
static void testFilteredCallShortCircuit(void)
{
    Logger_t logger;
    LoggerStatus_t status;

    printf("\nFiltered call short-circuits before module/fmt/backend are inspected:\n");

    /* No backend registered at all. */
    loggerInit(&logger, LOGGER_LEVEL_WARN);

    status = loggerWrite(&logger, LOGGER_LEVEL_DEBUG, NULL, NULL);
    TEST_ASSERT(status == LOGGER_SUCCESS,
                "a filtered call with NULL module, NULL fmt, and no backend still succeeds -- "
                "the classic case this design deliberately makes cheap");

    /* Same, but with a backend registered, to isolate that it's genuinely
     * the filtering (not backend absence) making this succeed. */
    loggerSetBackend(&logger, captureBackend, NULL);
    resetCapture();
    status = loggerWrite(&logger, LOGGER_LEVEL_DEBUG, NULL, NULL);
    TEST_ASSERT(status == LOGGER_SUCCESS, "same filtered call succeeds with a backend registered too");
    TEST_ASSERT(backendCallCount == 0, "the backend is never actually invoked for a filtered call");
}

/* -------------------------------------------------------------------------
 * Severity strings and exact [LEVEL][MODULE] message format
 * ------------------------------------------------------------------------- */
static void testEverySeverityStringAndExactFormat(void)
{
    Logger_t logger;
    struct
    {
        LoggerLevel_t level;
        const char *expectedTag;
    } cases[] = {
        {LOGGER_LEVEL_ERROR, "ERROR"},
        {LOGGER_LEVEL_WARN, "WARN"},
        {LOGGER_LEVEL_INFO, "INFO"},
        {LOGGER_LEVEL_DEBUG, "DEBUG"},
    };
    size_t i;
    char expected[64];

    printf("\nExact [LEVEL][MODULE] prefix for every severity:\n");

    loggerInit(&logger, LOGGER_LEVEL_DEBUG);
    loggerSetBackend(&logger, captureBackend, NULL);

    for (i = 0U; i < (sizeof(cases) / sizeof(cases[0])); i++)
    {
        resetCapture();
        loggerWrite(&logger, cases[i].level, "MOD", "hello");
        snprintf(expected, sizeof(expected), "[%s][MOD] hello", cases[i].expectedTag);
        TEST_ASSERT(strcmp(capturedMessage, expected) == 0, "message format matches [LEVEL][MODULE] message exactly");
    }
}

/* -------------------------------------------------------------------------
 * Formatting arguments
 * ------------------------------------------------------------------------- */
static void testFormattingArguments(void)
{
    Logger_t logger;

    printf("\nFormatting arguments (printf-style):\n");

    loggerInit(&logger, LOGGER_LEVEL_DEBUG);
    loggerSetBackend(&logger, captureBackend, NULL);

    resetCapture();
    loggerWrite(&logger, LOGGER_LEVEL_INFO, "MOD", "int=%d str=%s hex=0x%02X", 7, "text", 0xAU);
    TEST_ASSERT(strcmp(capturedMessage, "[INFO][MOD] int=7 str=text hex=0x0A") == 0,
                "multiple mixed format specifiers are applied correctly");

    resetCapture();
    loggerWrite(&logger, LOGGER_LEVEL_INFO, "MOD", "no arguments at all");
    TEST_ASSERT(strcmp(capturedMessage, "[INFO][MOD] no arguments at all") == 0,
                "a zero-argument format string (no %% specifiers) works correctly");

    resetCapture();
    loggerWrite(&logger, LOGGER_LEVEL_INFO, "MOD", "");
    TEST_ASSERT(strcmp(capturedMessage, "[INFO][MOD] ") == 0,
                "an empty format string produces just the prefix, with status success");
}

/* -------------------------------------------------------------------------
 * Truncation: long caller message, long module name, backend length,
 * message[length] == '\0'
 * ------------------------------------------------------------------------- */
static void testLongCallerMessageTruncation(void)
{
    Logger_t logger;
    LoggerStatus_t status;

    printf("\nLong caller-message truncation:\n");

    loggerInit(&logger, LOGGER_LEVEL_DEBUG);
    loggerSetBackend(&logger, captureBackend, NULL);
    resetCapture();

    status = loggerWrite(&logger, LOGGER_LEVEL_DEBUG, "MOD",
                          "this message is deliberately constructed to be far longer than "
                          "LOGGER_MAX_MESSAGE_LENGTH so that truncation must occur without "
                          "overflowing the fixed-size formatting buffer, no matter what");

    TEST_ASSERT(status == LOGGER_SUCCESS, "a call requiring truncation still reports SUCCESS");
    TEST_ASSERT(capturedLength < LOGGER_MAX_MESSAGE_LENGTH, "truncated message length stays under the configured maximum");
    TEST_ASSERT(capturedLength == strlen(capturedMessage), "backend-reported length matches the actual captured string length");
}

static void testLongModuleNameTruncation(void)
{
    Logger_t logger;
    char hugeModule[LOGGER_MAX_MESSAGE_LENGTH * 2];
    LoggerStatus_t status;

    printf("\nLong module-name (prefix) truncation:\n");

    memset(hugeModule, 'M', sizeof(hugeModule) - 1U);
    hugeModule[sizeof(hugeModule) - 1U] = '\0';

    loggerInit(&logger, LOGGER_LEVEL_DEBUG);
    loggerSetBackend(&logger, captureBackend, NULL);
    resetCapture();

    status = loggerWrite(&logger, LOGGER_LEVEL_ERROR, hugeModule, "trailing message content");

    TEST_ASSERT(status == LOGGER_SUCCESS, "a module name alone larger than the buffer still reports SUCCESS");
    TEST_ASSERT(capturedLength < LOGGER_MAX_MESSAGE_LENGTH, "prefix-only truncation stays under the configured maximum");
}

static void testMessageIsNullTerminatedIncludingOnTruncation(void)
{
    Logger_t logger;

    printf("\nmessage[length] == '\\0' holds, including on truncation:\n");

    loggerInit(&logger, LOGGER_LEVEL_DEBUG);
    loggerSetBackend(&logger, captureBackend, NULL);

    resetCapture();
    loggerWrite(&logger, LOGGER_LEVEL_INFO, "MOD", "short message");
    TEST_ASSERT(capturedMessage[capturedLength] == '\0', "null terminator present at exactly [length] for a normal message");

    resetCapture();
    loggerWrite(&logger, LOGGER_LEVEL_DEBUG, "MOD",
                "a very long message engineered to overflow the configured maximum message length repeatedly and reliably");
    TEST_ASSERT(capturedMessage[capturedLength] == '\0', "null terminator present at exactly [length] even when truncated");
}

/* -------------------------------------------------------------------------
 * Independent Logger instances / reentrancy, repeated logging
 * ------------------------------------------------------------------------- */
static void testIndependentLoggerInstances(void)
{
    Logger_t loggerA;
    Logger_t loggerB;

    printf("\nTwo independent Logger_t instances do not interfere:\n");

    loggerInit(&loggerA, LOGGER_LEVEL_DEBUG);
    loggerInit(&loggerB, LOGGER_LEVEL_DEBUG);
    loggerSetBackend(&loggerA, captureBackend, NULL);
    loggerSetBackend(&loggerB, captureBackendB, NULL);

    resetCapture();
    capturedLengthB = 0U;
    memset(capturedMessageB, 0, sizeof(capturedMessageB));

    loggerWrite(&loggerA, LOGGER_LEVEL_INFO, "A", "message from A");
    loggerWrite(&loggerB, LOGGER_LEVEL_INFO, "B", "message from B");

    TEST_ASSERT(strcmp(capturedMessage, "[INFO][A] message from A") == 0, "logger A's captured message is exactly its own");
    TEST_ASSERT(strcmp(capturedMessageB, "[INFO][B] message from B") == 0, "logger B's captured message is exactly its own, unaffected by A");
}

static void testRepeatedLogging(void)
{
    Logger_t logger;
    int i;

    printf("\nRepeated logging through the same context stays correct call after call:\n");

    loggerInit(&logger, LOGGER_LEVEL_DEBUG);
    loggerSetBackend(&logger, captureBackend, NULL);

    for (i = 0; i < 20; i++)
    {
        char expected[64];
        resetCapture();
        loggerWrite(&logger, LOGGER_LEVEL_INFO, "MOD", "iteration=%d", i);
        snprintf(expected, sizeof(expected), "[INFO][MOD] iteration=%d", i);
        TEST_ASSERT(strcmp(capturedMessage, expected) == 0, "each of 20 repeated calls produces its own correct message");
    }
}

int main(void)
{
    printf("Running logger tests...\n");

    testInitEveryValidThreshold();
    testInitInvalidThresholdAndNullLogger();
    testSetGetLevel();
    testBackendLifecycleAndContextPassthrough();
    testWriteNullLogger();
    testWriteNullModuleAndFmtWhenAllowed();
    testWriteInvalidSeverity();
    testRuntimeFilteringAtEveryThreshold();
    testLoggerLevelOffSuppressesEverything();
    testAllowedMessageNoBackend();
    testFilteredCallShortCircuit();
    testEverySeverityStringAndExactFormat();
    testFormattingArguments();
    testLongCallerMessageTruncation();
    testLongModuleNameTruncation();
    testMessageIsNullTerminatedIncludingOnTruncation();
    testIndependentLoggerInstances();
    testRepeatedLogging();

    printf("\n%d / %d tests passed.\n", passedTests, totalTests);

    return (passedTests == totalTests) ? 0 : 1;
}

/**************************************** END OF loggerTest.c ****************************************/