/******************************************************************************
 * @file   logger.c
 * @brief  Implementation of reusable embedded logging.
 *
 * @author Abhishek Doddagoudar
 *
 * @date   August 2026
 *
 * Implements the architecture, validation order, and formatting contract
 * documented in logger.h and docs/logger-design.md. The compile-time
 * filtering macros (LOGGER_ERROR/WARN/INFO/DEBUG) live entirely in
 * logger.h -- this file only implements loggerWrite() and the other
 * public functions those macros (and direct callers) resolve to.
 *
 * Thread Safety:
 * - Reentrant across independent Logger_t instances. Concurrent
 *   loggerWrite() calls into the SAME instance are safe with respect to
 *   Logger's own internal formatting state -- each call's buffer is
 *   stack-local, never shared or static, so two concurrent calls cannot
 *   corrupt each other's in-flight message. This does NOT mean the
 *   complete operation is unconditionally thread-safe end to end: two
 *   concurrent loggerWrite() calls on the same Logger_t can both invoke
 *   logger->backend(...) around the same time, and if that backend
 *   writes to a single shared, non-reentrant resource (one UART
 *   peripheral, for instance), synchronizing that is the registered
 *   backend's responsibility, not something Logger can guarantee on its
 *   behalf. Concurrent configuration calls (loggerSetLevel/
 *   loggerSetBackend/loggerClearBackend) racing against loggerWrite() on
 *   the same instance remain the caller's synchronization responsibility,
 *   as stated in logger.h.
 * - Reentrant is not the same claim as ISR-safe; see logger.h.
 *
 * Formatting Failures:
 * - snprintf()/vsnprintf() returning a negative value (an encoding
 *   error) is handled defensively rather than treated as a distinct
 *   failure mode: whatever valid, bounded, null-terminated content was
 *   already produced (the prefix, and/or as much of the caller's message
 *   as formatted successfully) is still passed to the backend, and
 *   loggerWrite() still returns LOGGER_SUCCESS. There is no
 *   LOGGER_ERROR_FORMATTING status in this release -- ordinary %s/%d/%u/
 *   %x-style format specifiers are not expected to hit this path in
 *   practice, and keeping the status model small was judged more
 *   valuable than a status code for a case this unlikely to occur. If
 *   this ever needs to be distinguished from a normal success, that's a
 *   deliberate future addition, not an oversight.
 *
 * Dynamic Allocation:
 * - None
 *
 * Dependencies:
 * - stdint.h, stddef.h, stdarg.h, stdio.h
 ******************************************************************************/

#include <logger.h>
#include <stdarg.h>
#include <stdio.h>

/**
 * @internal
 * @brief Validates a Logger context pointer.
 *
 * @param logger Context pointer to validate.
 *
 * @return LoggerStatus_t
 * @retval LOGGER_SUCCESS logger is non-NULL.
 * @retval LOGGER_ERROR_NULL_POINTER logger is NULL.
 */
static LoggerStatus_t validateLogger(const Logger_t *logger)
{
    if (logger == NULL)
    {
        return LOGGER_ERROR_NULL_POINTER;
    }
    return LOGGER_SUCCESS;
}

/**
 * @internal
 * @brief Validates a LoggerLevel_t intended for use as a threshold
 *        (loggerInit()/loggerSetLevel()) -- LOGGER_LEVEL_OFF is valid
 *        here.
 *
 * @param level Level to validate.
 *
 * @return LoggerStatus_t
 * @retval LOGGER_SUCCESS level is one of OFF, ERROR, WARN, INFO, DEBUG.
 * @retval LOGGER_ERROR_INVALID_LEVEL level is outside that range.
 */
static LoggerStatus_t validateThresholdLevel(LoggerLevel_t level)
{
    if ((level < LOGGER_LEVEL_OFF) || (level > LOGGER_LEVEL_DEBUG))
    {
        return LOGGER_ERROR_INVALID_LEVEL;
    }
    return LOGGER_SUCCESS;
}

/**
 * @internal
 * @brief Validates a LoggerLevel_t intended as a log call's own severity
 *        (loggerWrite()) -- LOGGER_LEVEL_OFF is NOT valid here; it is a
 *        threshold-only value (see logger.h and docs/logger-design.md,
 *        Section 2).
 *
 * @param level Level to validate.
 *
 * @return LoggerStatus_t
 * @retval LOGGER_SUCCESS level is one of ERROR, WARN, INFO, DEBUG.
 * @retval LOGGER_ERROR_INVALID_LEVEL level is LOGGER_LEVEL_OFF or outside
 *         the valid range entirely.
 */
static LoggerStatus_t validateMessageLevel(LoggerLevel_t level)
{
    if ((level < LOGGER_LEVEL_ERROR) || (level > LOGGER_LEVEL_DEBUG))
    {
        return LOGGER_ERROR_INVALID_LEVEL;
    }
    return LOGGER_SUCCESS;
}

/**
 * @internal
 * @brief Maps a validated message severity to its display string for the
 *        "[LEVEL][MODULE] " prefix (docs/logger-design.md, Section 14).
 *
 * @param level A level already confirmed valid by validateMessageLevel()
 *              (ERROR, WARN, INFO, or DEBUG).
 *
 * @return const char* Short, fixed, statically-allocated string. Never NULL.
 *
 * @note The default case is unreachable in normal operation -- loggerWrite()
 *       always validates level via validateMessageLevel() before this is
 *       called -- but is still handled explicitly rather than left as
 *       undefined behavior for a value outside the enum's intended range.
 */
static const char *loggerLevelToString(LoggerLevel_t level)
{
    switch (level)
    {
        case LOGGER_LEVEL_ERROR:
            return "ERROR";
        case LOGGER_LEVEL_WARN:
            return "WARN";
        case LOGGER_LEVEL_INFO:
            return "INFO";
        case LOGGER_LEVEL_DEBUG:
            return "DEBUG";
        case LOGGER_LEVEL_OFF:
        default:
            return "UNKNOWN";
    }
}

/**
 * @brief Initialize Logger state.
 *
 * @param logger           Pointer to Logger state.
 * @param initialThreshold Initial runtime severity threshold.
 *
 * @return LoggerStatus_t -- see logger.h for the full contract.
 */
LoggerStatus_t loggerInit(Logger_t *logger, LoggerLevel_t initialThreshold)
{
    LoggerStatus_t status = validateLogger(logger);

    if (status != LOGGER_SUCCESS)
    {
        return status;
    }

    status = validateThresholdLevel(initialThreshold);
    if (status != LOGGER_SUCCESS)
    {
        return status;
    }

    logger->backend = NULL;
    logger->backendContext = NULL;
    logger->runtimeThreshold = initialThreshold;

    return LOGGER_SUCCESS;
}

/**
 * @brief Register an output backend.
 *
 * @param logger         Pointer to Logger state.
 * @param backend        Backend function to receive formatted messages.
 * @param backendContext Opaque pointer passed through to every backend call.
 *
 * @return LoggerStatus_t -- see logger.h for the full contract.
 */
LoggerStatus_t loggerSetBackend(Logger_t *logger, LoggerBackend_t backend, void *backendContext)
{
    LoggerStatus_t status = validateLogger(logger);

    if (status != LOGGER_SUCCESS)
    {
        return status;
    }

    if (backend == NULL)
    {
        return LOGGER_ERROR_NULL_POINTER;
    }

    logger->backend = backend;
    logger->backendContext = backendContext;

    return LOGGER_SUCCESS;
}

/**
 * @brief Clear the registered backend.
 *
 * @param logger Pointer to Logger state.
 *
 * @return LoggerStatus_t -- see logger.h for the full contract.
 */
LoggerStatus_t loggerClearBackend(Logger_t *logger)
{
    LoggerStatus_t status = validateLogger(logger);

    if (status != LOGGER_SUCCESS)
    {
        return status;
    }

    logger->backend = NULL;
    logger->backendContext = NULL;

    return LOGGER_SUCCESS;
}

/**
 * @brief Set the runtime severity threshold.
 *
 * @param logger    Pointer to Logger state.
 * @param threshold New runtime severity threshold.
 *
 * @return LoggerStatus_t -- see logger.h for the full contract.
 */
LoggerStatus_t loggerSetLevel(Logger_t *logger, LoggerLevel_t threshold)
{
    LoggerStatus_t status = validateLogger(logger);

    if (status != LOGGER_SUCCESS)
    {
        return status;
    }

    status = validateThresholdLevel(threshold);
    if (status != LOGGER_SUCCESS)
    {
        return status; /* previous threshold left unchanged, per logger.h */
    }

    logger->runtimeThreshold = threshold;

    return LOGGER_SUCCESS;
}

/**
 * @brief Get the current runtime severity threshold.
 *
 * @param logger       Pointer to Logger state.
 * @param outThreshold Pointer to store the current runtime threshold.
 *
 * @return LoggerStatus_t -- see logger.h for the full contract.
 */
LoggerStatus_t loggerGetLevel(const Logger_t *logger, LoggerLevel_t *outThreshold)
{
    LoggerStatus_t status = validateLogger(logger);

    if (status != LOGGER_SUCCESS)
    {
        return status;
    }

    if (outThreshold == NULL)
    {
        return LOGGER_ERROR_NULL_POINTER;
    }

    *outThreshold = logger->runtimeThreshold;

    return LOGGER_SUCCESS;
}

/**
 * @brief Format and emit a log message.
 *
 * Implements the exact 7-step validation/filtering order documented in
 * logger.h and docs/logger-design.md, Section 11 -- a filtered call
 * (step 3) returns before module/fmt/backend (steps 4-6) are ever
 * inspected.
 *
 * @param logger Pointer to Logger state.
 * @param level  Severity of this message (ERROR, WARN, INFO, or DEBUG).
 * @param module Source/module identifier.
 * @param fmt    printf-style format string, followed by its arguments.
 *
 * @return LoggerStatus_t -- see logger.h for the full contract.
 */
LoggerStatus_t loggerWrite(Logger_t *logger, LoggerLevel_t level, const char *module, const char *fmt, ...)
{
    char buffer[LOGGER_MAX_MESSAGE_LENGTH];
    const char *levelStr;
    int prefixLen;
    size_t used;
    size_t remaining;
    va_list args;
    int msgLen;
    size_t msgWritten;
    size_t totalLen;
    LoggerStatus_t status = validateLogger(logger);

    if (status != LOGGER_SUCCESS)
    {
        return status;
    }

    status = validateMessageLevel(level);
    if (status != LOGGER_SUCCESS)
    {
        return status;
    }

    if ((uint32_t)level > (uint32_t)logger->runtimeThreshold)
    {
        return LOGGER_SUCCESS; /* filtered -- nothing below this point is inspected */
    }

    if (module == NULL)
    {
        return LOGGER_ERROR_NULL_POINTER;
    }

    if (fmt == NULL)
    {
        return LOGGER_ERROR_NULL_POINTER;
    }

    if (logger->backend == NULL)
    {
        return LOGGER_ERROR_NO_BACKEND;
    }

    /* --- Prefix: "[LEVEL][MODULE] " --- */
    levelStr = loggerLevelToString(level);
    prefixLen = snprintf(buffer, sizeof(buffer), "[%s][%s] ", levelStr, module);

    if (prefixLen < 0)
    {
        /* Encoding error from snprintf itself -- buffer's contents are
         * not guaranteed null-terminated by the standard in this case,
         * so terminate it explicitly rather than trust it. */
        used = 0U;
        buffer[0] = '\0';
    }
    else if ((size_t)prefixLen < sizeof(buffer))
    {
        used = (size_t)prefixLen;
    }
    else
    {
        /* Prefix alone would have consumed the whole buffer -- clamp to
         * leave room for at least a null terminator, per
         * docs/logger-design.md, Section 9. */
        used = sizeof(buffer) - 1U;
    }

    remaining = sizeof(buffer) - used; /* always >= 1, since used <= sizeof(buffer) - 1 */

    /* --- Caller's formatted message, into whatever space is left --- */
    va_start(args, fmt);
    msgLen = vsnprintf(buffer + used, remaining, fmt, args);
    va_end(args);

    if (msgLen < 0)
    {
        /* Same encoding-error reasoning as the prefix above. */
        msgWritten = 0U;
        buffer[used] = '\0';
    }
    else if ((size_t)msgLen < remaining)
    {
        /* Message fit entirely -- includes the legitimate case of a
         * zero-length formatted message (msgLen == 0). */
        msgWritten = (size_t)msgLen;
    }
    else
    {
        /* Truncated: only what actually fit before the null terminator
         * was written. */
        msgWritten = remaining - 1U;
    }

    totalLen = used + msgWritten;

    logger->backend(buffer, totalLen, logger->backendContext);

    return LOGGER_SUCCESS;
}

/*****************************************End of logger.c*****************************************/