/******************************************************************************
 * @file   logger.h
 * @brief  Public interface for reusable embedded logging.
 *
 * @author Abhishek Doddagoudar
 *
 * @date   August 2026
 *
 * Provides a consistent mechanism for reporting diagnostic, informational,
 * warning, and error messages from ECU software modules, with a pluggable
 * output backend and both runtime and compile-time severity filtering.
 *
 * Architecture:
 *
 *      Call site
 *          |
 *      LOGGER_ERROR/WARN/INFO/DEBUG(logger, module, fmt, ...)  <- macro layer
 *          |                                                      (compile-time gate)
 *          v
 *      loggerWrite(logger, level, module, fmt, ...)             <- function layer
 *          |                                                      (runtime gate,
 *          |                                                       formatting,
 *          v                                                       backend dispatch)
 *      backend(message, length, backendContext)
 *
 *      loggerWrite() is always compiled and always present, regardless of
 *      compile-time filtering -- it can be called directly, bypassing the
 *      macros, for testing or advanced use. See docs/logger-design.md,
 *      Section 1.
 *
 * Filtering:
 *      A log call must pass BOTH the compile-time threshold
 *      (LOGGER_COMPILE_TIME_LEVEL) and the runtime threshold
 *      (Logger_t.runtimeThreshold) before formatting or the backend are
 *      ever invoked. See docs/logger-design.md, Section 1 and Section 3.
 *
 *      Logging calls below the compile-time threshold are excluded from
 *      the logging execution path in the generated source, and their
 *      arguments are NOT evaluated -- see the LOGGER_ERROR/WARN/INFO/DEBUG
 *      macro documentation below, and docs/logger-design.md, Section 5,
 *      for exactly what this guarantees and why.
 *
 * Suppressing all output:
 *      There is no separate enable/disable flag. Set the runtime
 *      threshold to LOGGER_LEVEL_OFF instead. See docs/logger-design.md,
 *      Section 2.
 *
 * This module is standalone with respect to the rest of this project --
 * it includes nothing from crc16/crc32/packetParser/memoryPool/
 * ringBuffer/softwareTimer, and none of them need to include this header
 * to be "loggable." Wiring existing modules to actually call into Logger
 * is explicitly out of scope for this module (docs/logger-design.md,
 * Section 16) and is separate follow-up work.
 *
 * Features:
 *      - Pluggable output backend (UART, console, host-side test capture,
 *        or any other sink the caller supplies)
 *      - printf-style formatted messages
 *      - Runtime AND compile-time severity filtering
 *      - No dynamic memory allocation
 *      - Reentrant formatting path (fresh stack-local buffer per call)
 *
 * Thread Safety:
 *      Reentrant across independent Logger_t instances. Concurrent
 *      loggerWrite() calls into the SAME instance are safe with respect
 *      to Logger's own internal formatting state -- each call's buffer
 *      is stack-local, not shared (see docs/logger-design.md, Section
 *      8), so two concurrent calls cannot corrupt each other's in-flight
 *      message. This does NOT mean the complete logging operation is
 *      unconditionally thread-safe end to end: two concurrent
 *      loggerWrite() calls on the same Logger_t can both invoke the
 *      registered backend around the same time, and if that backend
 *      writes to a single shared, non-reentrant resource (one UART
 *      peripheral, for instance), synchronizing that is the backend's
 *      responsibility, not something this module guarantees on its
 *      behalf. Concurrent loggerSetLevel()/loggerSetBackend() calls
 *      racing against loggerWrite() on the same instance remain a
 *      genuine data race and must be synchronized by the caller, same
 *      as every other module in this project.
 *
 *      IMPORTANT: reentrant is NOT the same claim as ISR-safe. This
 *      module makes no claim of being safe to call from an interrupt
 *      service routine -- vsnprintf() in particular is not evaluated for
 *      ISR suitability by this design. See docs/logger-design.md,
 *      Section 12.
 *
 * Dynamic Memory:
 *      None
 *
 * Example Usage:
 *
 *      static void uartBackend(const char *message, size_t length, void *context)
 *      {
 *          (void)context;
 *          uartTransmit((const uint8_t *)message, length);
 *      }
 *
 *      Logger_t logger;
 *      loggerInit(&logger, LOGGER_LEVEL_INFO);
 *      loggerSetBackend(&logger, uartBackend, NULL);
 *
 *      LOGGER_ERROR(&logger, "CRC32", "checksum mismatch: expected 0x%08X, got 0x%08X", a, b);
 *      LOGGER_DEBUG(&logger, "CRC32", "processed %zu bytes", len); // no-op unless
 *                                                                  // LOGGER_COMPILE_TIME_LEVEL
 *                                                                  // includes DEBUG
 *
 ******************************************************************************/

#ifndef LOGGER_H
#define LOGGER_H

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Log severity levels, in ascending order of verbosity.
 *
 * LOGGER_LEVEL_OFF is a threshold-only value: it must never be passed as
 * a log call's own severity (LOGGER_ERROR/WARN/INFO/DEBUG never do this),
 * only assigned to Logger_t.runtimeThreshold to suppress all output.
 * loggerWrite() rejects LOGGER_LEVEL_OFF (and any other out-of-range
 * value) as an invalid level if passed as a call's severity -- see
 * loggerWrite()'s documentation below.
 */
typedef enum
{
    LOGGER_LEVEL_OFF = 0,
    LOGGER_LEVEL_ERROR,
    LOGGER_LEVEL_WARN,
    LOGGER_LEVEL_INFO,
    LOGGER_LEVEL_DEBUG
} LoggerLevel_t;

typedef enum
{
    LOGGER_SUCCESS = 0,
    LOGGER_ERROR_NULL_POINTER,
    LOGGER_ERROR_NO_BACKEND,
    LOGGER_ERROR_INVALID_LEVEL
} LoggerStatus_t;

/**
 * @brief Output backend function signature.
 *
 * Receives the fully-formatted, bounded message -- already assembled
 * with its "[LEVEL][MODULE] " prefix and the caller's format string and
 * arguments applied. The backend's job is purely to deliver this text
 * somewhere (UART, console, a test-capture buffer, etc.); it has no
 * visibility into severity, module, or the original format string
 * separately.
 *
 * @param message Pointer to the formatted, null-terminated message.
 *                message[length] == '\0' always holds, including when
 *                the message was truncated (see loggerWrite()'s
 *                documentation below) -- length never includes that
 *                terminator either way. Valid only for the duration of
 *                the backend call -- the backend must not retain this
 *                pointer.
 * @param length  Length of message in bytes, NOT including the null
 *                terminator, and reflecting the message's actual
 *                (possibly truncated) length -- never the length that
 *                would have resulted without truncation. Provided so
 *                the backend doesn't need to strlen() the message
 *                itself.
 * @param context Opaque pointer, passed through unchanged from whatever
 *                was registered via loggerSetBackend(). Not interpreted
 *                or dereferenced by Logger itself.
 */
typedef void (*LoggerBackend_t)(const char *message, size_t length, void *context);

/**
 * @brief Logger state.
 *
 * Caller-owned, matching every other module in this project (CRC32,
 * ring buffer, software timer, memory pool). No library-global logger
 * instance exists.
 *
 * @note backend is NULL immediately after loggerInit() -- registering a
 *       backend is a separate, explicit step (loggerSetBackend()). See
 *       loggerInit()'s documentation below.
 *
 * @note Future extensions may add additional members. Do not assume the
 *       struct's size or layout is stable across versions.
 */
typedef struct
{
    LoggerBackend_t backend;
    void *backendContext;
    LoggerLevel_t runtimeThreshold;
} Logger_t;

/* ---------------------------------------------------------------------
 * Configuration macros
 *
 * All are #ifndef-guarded so a build can override any of them with a
 * -D flag (e.g. -DLOGGER_COMPILE_TIME_LEVEL=LOGGER_LEVEL_INFO_VAL for a
 * release build), with no changes to this header required.
 * ------------------------------------------------------------------- */

/* Plain integer mirrors of LoggerLevel_t, required because the
 * preprocessor's #if directives cannot evaluate C enum values -- only
 * integer constant expressions. Kept numerically identical to
 * LoggerLevel_t above. Not intended to be used anywhere except in
 * #if comparisons against LOGGER_COMPILE_TIME_LEVEL. */
#define LOGGER_LEVEL_OFF_VAL   (0)
#define LOGGER_LEVEL_ERROR_VAL (1)
#define LOGGER_LEVEL_WARN_VAL  (2)
#define LOGGER_LEVEL_INFO_VAL  (3)
#define LOGGER_LEVEL_DEBUG_VAL (4)

#ifndef LOGGER_COMPILE_TIME_LEVEL
#define LOGGER_COMPILE_TIME_LEVEL LOGGER_LEVEL_DEBUG_VAL /* default: every severity compiled in */
#endif

#if (LOGGER_COMPILE_TIME_LEVEL < LOGGER_LEVEL_OFF_VAL) || (LOGGER_COMPILE_TIME_LEVEL > LOGGER_LEVEL_DEBUG_VAL)
#error "LOGGER_COMPILE_TIME_LEVEL must be one of the LOGGER_LEVEL_*_VAL constants (OFF..DEBUG)"
#endif

/** Maximum length, in bytes, of a single formatted log message
 *  (including its "[LEVEL][MODULE] " prefix), backing the stack-local
 *  formatting buffer inside loggerWrite(). Messages that would exceed
 *  this are truncated -- see loggerWrite()'s documentation below. */
#ifndef LOGGER_MAX_MESSAGE_LENGTH
#define LOGGER_MAX_MESSAGE_LENGTH (128U)
#endif

/* A buffer of 0 or 1 bytes cannot hold even an empty, null-terminated
 * message, and makes the truncation/remaining-space arithmetic in
 * loggerWrite() meaningless. This is a build configuration error, not a
 * runtime one, so it's caught here rather than left to be discovered as
 * undefined behavior at some later point. */
#if LOGGER_MAX_MESSAGE_LENGTH < 2U
#error "LOGGER_MAX_MESSAGE_LENGTH must be at least 2"
#endif

/**
 * @brief Initialize Logger state.
 *
 * On success, sets:
 *      logger->backend          = NULL
 *      logger->backendContext   = NULL
 *      logger->runtimeThreshold = initialThreshold
 *
 * Deliberately does NOT register a backend -- logger->backend is NULL
 * after this call. Logging calls that pass both filters with no backend
 * registered return LOGGER_ERROR_NO_BACKEND (see loggerWrite()) until
 * loggerSetBackend() is called.
 *
 * @param logger           Pointer to Logger state.
 * @param initialThreshold Initial runtime severity threshold. Must be
 *                         one of LOGGER_LEVEL_OFF, _ERROR, _WARN, _INFO,
 *                         or _DEBUG -- unlike loggerWrite()'s level
 *                         parameter, LOGGER_LEVEL_OFF IS valid here
 *                         (start fully suppressed).
 *
 * @return
 *      LOGGER_SUCCESS             -> logger initialized as above
 *      LOGGER_ERROR_NULL_POINTER  -> logger == NULL; no operation performed
 *      LOGGER_ERROR_INVALID_LEVEL -> initialThreshold is not one of the
 *                                     five valid LoggerLevel_t values;
 *                                     no operation performed
 */
LoggerStatus_t loggerInit(Logger_t *logger, LoggerLevel_t initialThreshold);

/**
 * @brief Register an output backend.
 *
 * @param logger         Pointer to Logger state.
 * @param backend        Backend function to receive formatted messages.
 * @param backendContext Opaque pointer passed through unchanged to every
 *                        backend call. May be NULL if the backend doesn't
 *                        need it.
 *
 * @return
 *      LOGGER_SUCCESS            -> backend registered
 *      LOGGER_ERROR_NULL_POINTER -> logger == NULL or backend == NULL
 */
LoggerStatus_t loggerSetBackend(Logger_t *logger, LoggerBackend_t backend, void *backendContext);

/**
 * @brief Clear the registered backend.
 *
 * On success, sets both:
 *      logger->backend        = NULL
 *      logger->backendContext = NULL
 *
 * (Not just backend alone -- a stale backendContext left behind after
 * clearing the backend it belonged to would be a latent hazard the next
 * time a backend is registered without also passing a fresh context.)
 * After this call, log calls that pass both filters return
 * LOGGER_ERROR_NO_BACKEND until a new backend is registered via
 * loggerSetBackend().
 *
 * @param logger Pointer to Logger state.
 *
 * @return
 *      LOGGER_SUCCESS            -> backend and backendContext both cleared
 *      LOGGER_ERROR_NULL_POINTER -> logger == NULL
 */
LoggerStatus_t loggerClearBackend(Logger_t *logger);

/**
 * @brief Set the runtime severity threshold.
 *
 * To suppress all output, set threshold to LOGGER_LEVEL_OFF -- there is
 * no separate enable/disable flag.
 *
 * @param logger    Pointer to Logger state.
 * @param threshold New runtime severity threshold. Must be one of
 *                  LOGGER_LEVEL_OFF, _ERROR, _WARN, _INFO, or _DEBUG --
 *                  as with loggerInit(), LOGGER_LEVEL_OFF IS valid here.
 *
 * @return
 *      LOGGER_SUCCESS             -> threshold updated
 *      LOGGER_ERROR_NULL_POINTER  -> logger == NULL
 *      LOGGER_ERROR_INVALID_LEVEL -> threshold is not one of the five
 *                                     valid LoggerLevel_t values; no
 *                                     operation performed (the previous
 *                                     threshold is left unchanged)
 */
LoggerStatus_t loggerSetLevel(Logger_t *logger, LoggerLevel_t threshold);

/**
 * @brief Get the current runtime severity threshold.
 *
 * @param logger    Pointer to Logger state.
 * @param outThreshold Current runtime severity threshold.
 *
 * @return
 *      LOGGER_SUCCESS            -> *outThreshold set
 *      LOGGER_ERROR_NULL_POINTER -> logger == NULL or outThreshold == NULL
 */
LoggerStatus_t loggerGetLevel(const Logger_t *logger, LoggerLevel_t *outThreshold);

/**
 * @brief Format and emit a log message.
 *
 * This is the function the LOGGER_ERROR/WARN/INFO/DEBUG macros below
 * expand to when compiled in. Unlike those macros, this function is
 * ALWAYS present regardless of LOGGER_COMPILE_TIME_LEVEL, and always
 * performs its own runtime check -- it can be called directly (bypassing
 * the macros and their compile-time elimination) for testing or advanced
 * use, with the full validation/return-status contract below.
 *
 * @note Unlike the LOGGER_* macros, this is an ordinary function: all of
 *       its arguments ARE evaluated on every call, regardless of level
 *       or threshold. Compile-time argument-non-evaluation is a property
 *       of the macros only (see below) -- calling loggerWrite() directly
 *       does not get that benefit.
 *
 * Validation and filtering order (all of the following happen in this
 * exact sequence; a call that is filtered at step 3 never reaches steps
 * 4 onward, so module/fmt/backend are never inspected for a filtered
 * call):
 *
 *      1. logger == NULL?                    -> LOGGER_ERROR_NULL_POINTER
 *      2. level not one of the four real
 *         severities (LOGGER_LEVEL_OFF is
 *         NOT valid here)?                    -> LOGGER_ERROR_INVALID_LEVEL
 *      3. level > logger->runtimeThreshold?   -> LOGGER_SUCCESS (filtered;
 *                                                 nothing below this point
 *                                                 is evaluated)
 *      4. module == NULL?                     -> LOGGER_ERROR_NULL_POINTER
 *      5. fmt == NULL?                        -> LOGGER_ERROR_NULL_POINTER
 *      6. logger->backend == NULL?            -> LOGGER_ERROR_NO_BACKEND
 *      7. format (truncating silently if the
 *         result would exceed
 *         LOGGER_MAX_MESSAGE_LENGTH - 1
 *         characters), call the backend       -> LOGGER_SUCCESS
 *                                                 (see LoggerBackend_t's
 *                                                 documentation above for
 *                                                 the exact null-
 *                                                 termination/length
 *                                                 contract the backend
 *                                                 receives, including on
 *                                                 truncation)
 *
 * @param logger Pointer to Logger state.
 * @param level  Severity of this message. Must be LOGGER_LEVEL_ERROR,
 *               LOGGER_LEVEL_WARN, LOGGER_LEVEL_INFO, or
 *               LOGGER_LEVEL_DEBUG -- LOGGER_LEVEL_OFF is a
 *               threshold-only value and is rejected here.
 * @param module Source/module identifier (e.g. "CRC32"). Any string is
 *               accepted -- Logger has no registry of valid module
 *               names.
 * @param fmt    printf-style format string, followed by its arguments.
 *
 * @return See the validation/filtering order above.
 */
LoggerStatus_t loggerWrite(Logger_t *logger, LoggerLevel_t level, const char *module, const char *fmt, ...);

/* ---------------------------------------------------------------------
 * Compile-time-filterable logging macros.
 *
 * IMPORTANT -- these are statement macros, not expressions:
 *      Depending on LOGGER_COMPILE_TIME_LEVEL, LOGGER_ERROR(...) expands
 *      to EITHER a call to loggerWrite() (evaluating to LoggerStatus_t)
 *      OR to ((void)0) (evaluating to void). Code that assigns, compares,
 *      or otherwise uses the "return value" of these macros will compile
 *      in whichever build configuration happens to compile that severity
 *      in, and FAIL TO COMPILE the moment that severity is excluded --
 *      a latent, configuration-dependent build break. Always use these
 *      as bare statements:
 *
 *          LOGGER_ERROR(logger, "MOD", "message");   // correct
 *          LoggerStatus_t s = LOGGER_ERROR(...);     // WRONG -- do not do this
 *
 *      Call loggerWrite() directly if the status of a specific log
 *      attempt is genuinely needed.
 *
 * Argument non-evaluation when compiled out:
 *      When a severity is excluded by LOGGER_COMPILE_TIME_LEVEL, its
 *      macro expands to ((void)0) -- a bare no-op expression that does
 *      not reference logger/module/fmt/the variadic arguments anywhere
 *      in its expansion. Tokens that appear at a macro's call site but
 *      not in its expansion are never seen by the compiler as an
 *      expression, and are therefore never evaluated -- so
 *      LOGGER_DEBUG(logger, "MOD", "%d", expensiveFunction()) with
 *      DEBUG excluded does NOT call expensiveFunction(). See
 *      docs/logger-design.md, Section 5, for the full reasoning
 *      (including why this could not be achieved with a no-op function
 *      call instead of ((void)0)).
 *
 *      Known consequence of this, not a defect: a variable computed
 *      solely to be passed into a compiled-out log call (e.g.
 *      `int x = expensiveFunction(); LOGGER_DEBUG(logger, "MOD", "%d", x);`
 *      with DEBUG excluded) will trigger an ordinary -Wunused-variable
 *      warning on x once the log call expands to ((void)0), since x's
 *      only use has disappeared from the expansion. This is inherent to
 *      any compile-time log-elimination scheme, not something this
 *      header works around -- if x has no other use, that's a genuine
 *      signal the surrounding code should be restructured, not silenced.
 *
 * Format-string safety:
 *      This project's build does not currently apply a compiler
 *      format-string-checking attribute (e.g. GCC/Clang's
 *      __attribute__((format(printf, ...)))) to these macros/loggerWrite().
 *      A mismatch between fmt's conversion specifiers and the actual
 *      variadic arguments supplied is a caller error this module cannot
 *      detect at runtime in portable C.
 * ------------------------------------------------------------------- */

#if LOGGER_COMPILE_TIME_LEVEL >= LOGGER_LEVEL_ERROR_VAL
#define LOGGER_ERROR(logger, module, ...) loggerWrite((logger), LOGGER_LEVEL_ERROR, (module), __VA_ARGS__)
#else
#define LOGGER_ERROR(logger, module, ...) ((void)0)
#endif

#if LOGGER_COMPILE_TIME_LEVEL >= LOGGER_LEVEL_WARN_VAL
#define LOGGER_WARN(logger, module, ...) loggerWrite((logger), LOGGER_LEVEL_WARN, (module), __VA_ARGS__)
#else
#define LOGGER_WARN(logger, module, ...) ((void)0)
#endif

#if LOGGER_COMPILE_TIME_LEVEL >= LOGGER_LEVEL_INFO_VAL
#define LOGGER_INFO(logger, module, ...) loggerWrite((logger), LOGGER_LEVEL_INFO, (module), __VA_ARGS__)
#else
#define LOGGER_INFO(logger, module, ...) ((void)0)
#endif

#if LOGGER_COMPILE_TIME_LEVEL >= LOGGER_LEVEL_DEBUG_VAL
#define LOGGER_DEBUG(logger, module, ...) loggerWrite((logger), LOGGER_LEVEL_DEBUG, (module), __VA_ARGS__)
#else
#define LOGGER_DEBUG(logger, module, ...) ((void)0)
#endif

#endif /* LOGGER_H */

/******************************** END OF logger.h ********************************/