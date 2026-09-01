# Logger Design

Resolves the 16 open design questions left by `logger-requirements.md`
with concrete decisions and the reasoning behind each, the way
`crc32-design.md` and `memoryPool-design.md` did before their headers
were written. `logger.h` is not written yet.

---

## 1. Logger Architecture

Two layers, split by *when* filtering happens, matching the two-mechanism
requirement locked in requirements:

```
Call site
    |
LOGGER_ERROR/WARN/INFO/DEBUG(logger, module, fmt, ...)   <- macro layer
    |                                                     (compile-time gate)
    v
loggerWrite(logger, level, module, fmt, ...)           <- function layer
    |                                                     (runtime gate,
    |                                                      formatting,
    v                                                      backend dispatch)
backend(message, length, backendContext)
```

The macro layer is the *only* place compile-time exclusion happens --
`loggerWrite()` itself is always compiled, always present, and always
does its own runtime check unconditionally. This split matters: it means
`loggerWrite()` can be called directly (bypassing the macros entirely)
for testing or advanced use, with well-defined behavior, while ordinary
call sites go through the macros and get compile-time elimination for
free.

State lives in a caller-owned `Logger_t` context (Section 6) -- matching
every other module in this project (`crc16`, `crc32`, `memoryPool`,
`ringBuffer`, `softwareTimer`) rather than a single library-global
logger. This directly satisfies the "no unnecessary global mutable
state" guarantee and lets unit tests run multiple independent loggers
(e.g. one per test case) without interference.

## 2. Severity-Level Representation

```c
typedef enum
{
    LOGGER_LEVEL_OFF = 0,
    LOGGER_LEVEL_ERROR,
    LOGGER_LEVEL_WARN,
    LOGGER_LEVEL_INFO,
    LOGGER_LEVEL_DEBUG
} LoggerLevel_t;
```

Ascending verbosity, `OFF` first. `OFF` is never a valid severity for an
*actual* log call (`LOGGER_ERROR`/`WARN`/`INFO`/`DEBUG` never pass it as
their own level) -- it exists solely as a threshold value, which is
exactly what satisfies the locked "`LOGGER_LEVEL_OFF` instead of a
separate enable flag" requirement: setting `runtimeThreshold =
LOGGER_LEVEL_OFF` suppresses every real severity, since every real
severity is numerically greater than `OFF`.

## 3. Runtime Filtering

**Invalid `level` values are rejected before filtering is even
attempted.** `loggerWrite()` accepts a plain `LoggerLevel_t`, and nothing
in C stops a caller from passing a value outside the four real
severities (e.g. an out-of-range cast, or accidentally passing
`LOGGER_LEVEL_OFF` itself as a call's *own* level rather than as a
threshold). Comparing a garbage or out-of-range level against the
threshold would silently do something -- filter it, or let it through --
based on whatever numeric value it happened to hold, which is exactly
the kind of undefined-by-omission behavior this project has avoided
everywhere else (e.g. `SoftwareTimerMode_t`'s explicit
`SOFTWARE_TIMER_ERROR_INVALID_MODE`). **Locked: `loggerWrite()`
validates `level` is one of `LOGGER_LEVEL_ERROR`, `LOGGER_LEVEL_WARN`,
`LOGGER_LEVEL_INFO`, or `LOGGER_LEVEL_DEBUG`** (explicitly excluding
`LOGGER_LEVEL_OFF`, which is a threshold-only value -- see Section 2)
**before doing anything else, and returns `LOGGER_ERROR_INVALID_LEVEL`
(Section 11) for anything else.** In ordinary use this can only be
reached by calling `loggerWrite()` directly with a bad value, since the
`LOGGER_ERROR`/`WARN`/`INFO`/`DEBUG` macros (Section 4) only ever pass
one of the four real severities -- but `loggerWrite()` is a public,
directly-callable function (Section 1), so it validates its own
parameters rather than trusting only the macros to behave.

```c
if ((level < LOGGER_LEVEL_ERROR) || (level > LOGGER_LEVEL_DEBUG))
{
    return LOGGER_ERROR_INVALID_LEVEL;
}

if ((uint32_t)level > (uint32_t)logger->runtimeThreshold)
{
    return LOGGER_SUCCESS; /* filtered -- routine, not an error (Section 11) */
}
```

A call is emitted only if its severity is *at or below* the threshold
(i.e. at least as important as the threshold demands) -- `runtimeThreshold
= LOGGER_LEVEL_WARN` allows `ERROR` and `WARN` calls through, filters
`INFO` and `DEBUG`. This is the single runtime check inside
`loggerWrite()`, and it runs *after* the compile-time gate has already
decided the call exists at all (Section 1's ordering).

## 4. Compile-Time Filtering Macros

**Naming: `LOGGER_ERROR`/`LOGGER_WARN`/`LOGGER_INFO`/`LOGGER_DEBUG`, not
the shorter `LOG_*`.** A generic `LOG_ERROR` is a plausible name for
*any* logging library to claim, and this project is a reusable library
other application code will link against -- a bare `LOG_ERROR` has real
collision potential with another header the same translation unit
includes. Prefixing with `LOGGER_` costs four characters at every call
site and removes that risk entirely, consistent with this project
already prefixing every other public identifier with its module name
(`CRC32_*`, `SOFTWARE_TIMER_*`, `RING_BUFFER_*`).

**The `#if` problem:** the preprocessor cannot evaluate `LoggerLevel_t`
enum values in `#if` directives -- `#if` only understands integer
constant expressions, not C enums. So a second, parallel set of plain
integer macros is required, kept numerically identical to the enum:

```c
#define LOGGER_LEVEL_OFF_VAL   0
#define LOGGER_LEVEL_ERROR_VAL 1
#define LOGGER_LEVEL_WARN_VAL  2
#define LOGGER_LEVEL_INFO_VAL  3
#define LOGGER_LEVEL_DEBUG_VAL 4

#ifndef LOGGER_COMPILE_TIME_LEVEL
#define LOGGER_COMPILE_TIME_LEVEL LOGGER_LEVEL_DEBUG_VAL /* default: everything compiled in */
#endif
```

The `#ifndef` guard around the default lets a build system override the
threshold with `-DLOGGER_COMPILE_TIME_LEVEL=LOGGER_LEVEL_INFO_VAL`
without editing `logger.h` -- the standard embedded pattern for
build-variant configuration (debug build vs. release image).

Each severity gets its own independently-guarded macro pair:

```c
#if LOGGER_COMPILE_TIME_LEVEL >= LOGGER_LEVEL_ERROR_VAL
#define LOGGER_ERROR(logger, module, ...) loggerWrite((logger), LOGGER_LEVEL_ERROR, (module), __VA_ARGS__)
#else
#define LOGGER_ERROR(logger, module, ...) ((void)0)
#endif

/* LOGGER_WARN, LOGGER_INFO, LOGGER_DEBUG follow the same pattern against their
 * own LOGGER_LEVEL_*_VAL threshold. */
```

## 5. Preventing Argument Evaluation When Compiled Out

Two decisions here, both load-bearing for the locked requirement that
*"when a log call is excluded at compile time, its arguments shall not be
evaluated"* (e.g. `LOGGER_DEBUG(lg, "MOD", "%d", expensiveFunction())` with
`DEBUG` excluded must not call `expensiveFunction()`).

**Decision A -- the disabled expansion is `((void)0)`, not a function
call.** The macro parameters (`logger`, `module`, `...`) are declared in
the macro's parameter list (required for the variadic syntax to parse at
all), but the *body* of the disabled branch never references them. In C,
tokens that appear in a macro invocation but don't appear in the
expansion are simply dropped -- they never reach the compiler as an
expression, so nothing about them is ever evaluated. This is why the
disabled path cannot be something like a no-op function call
(`loggerNoOp(logger, module, ...)`) -- ordinary C function-call semantics
require every argument to be evaluated before the call happens,
regardless of what the function does with them. `((void)0)` sidesteps
that entirely: there is no call, so there is nothing to evaluate.

**Decision B -- the format string is folded into `...`, not given its
own named parameter.** This is what avoids the classic
zero-variadic-argument trailing-comma problem
(`#define LOGGER_ERROR(fmt, ...) loggerWrite(fmt, __VA_ARGS__)` called as
`LOGGER_ERROR("static message")` expands to a trailing `, )` -- a syntax
error in strict C99) without reaching for the GNU `##__VA_ARGS__`
extension, which this project's `-pedantic` compilation requirement
would flag. By writing `LOGGER_ERROR(logger, module, ...)` with the format
string as part of `...` instead of a named parameter before it,
`__VA_ARGS__` is *never* empty for a valid call (a log call always
supplies at least a format string), so the comma-before-empty-
`__VA_ARGS__` case this project would otherwise hit never arises. This
keeps the macros portable C99, clean under `-std=c99 -Wall -Wextra
-pedantic -Wconversion -Wshadow` with no GNU-extension carve-out needed
anywhere in this module.

## 6. Logger Context

```c
typedef struct
{
    LoggerBackend_t backend;        /* NULL until loggerSetBackend() is called */
    void *backendContext;           /* opaque; passed through to backend, unused by Logger itself */
    LoggerLevel_t runtimeThreshold;
} Logger_t;
```

Caller-owned, three members, matching this project's established
pattern (`CRC32_Context_t`, `RingBuffer_t`, `SoftwareTimerPool_t`) of
putting all state in a struct the caller allocates and owns -- no
library-global logger instance. `loggerInit()` sets `backend = NULL`
(deliberately -- see Section 13) and `runtimeThreshold` to the
caller-supplied initial value.

## 7. Backend Callback Interface

```c
typedef void (*LoggerBackend_t)(const char *message, size_t length, void *context);
```

Three decisions folded into this one signature:

- **The backend receives the fully-formatted string, not the raw level/
  module/format/args.** All of that has already been assembled into one
  string by `loggerWrite()` before the backend is ever called (message
  format decided in Section 14) -- the backend's job is purely "here is
  text, do something with it." This keeps every backend implementation
  (UART driver, host-side test capture, a future flash trace buffer)
  equally trivial to write, and is what makes the pluggable-backend
  decision actually pay off for testability -- a test backend can be a
  five-line function that appends to a buffer.
- **`length` is passed alongside `message`, not left for the backend to
  discover via `strlen()`.** `loggerWrite()` already knows the exact
  formatted length (from `vsnprintf`'s return value, clamped per Section
  9), so handing it over directly saves every backend implementation
  from re-scanning the string -- a small but real cost saving on a UART
  driver that would otherwise `strlen()` before every transmission.
- **`context` mirrors `SoftwareTimerCallback_t`'s `context` parameter
  exactly** (`typedef void (*SoftwareTimerCallback_t)(TimerHandle_t
  handle, void *context)`) -- same pattern, same project, same reason: it
  lets one backend function serve multiple loggers or carry
  backend-specific state (e.g. which UART peripheral) without any global
  variable.

## 8. Formatting Buffer Ownership

**Locked: a stack-local buffer, declared inside `loggerWrite()` itself,
fresh on every call.**

```c
LoggerStatus_t loggerWrite(Logger_t *logger, LoggerLevel_t level, const char *module, const char *fmt, ...)
{
    char buffer[LOGGER_MAX_MESSAGE_LENGTH];
    /* ... */
}
```

This is what makes the "no shared mutable formatting state between
independent logging calls" requirement true by construction rather than
by discipline: each call gets its own stack frame, and therefore its own
copy of `buffer`, automatically. Two calls on genuinely independent call
stacks (e.g. two RTOS tasks, if this platform grows one) cannot possibly
corrupt each other's buffer, because there is no *one* buffer to
corrupt -- unlike a `static char buffer[...]` inside `loggerWrite()`,
which would be shared and racy, or a buffer inside `Logger_t` itself,
which would make two calls sharing the *same logger* (a very normal
thing to do -- most of a program logging through one shared `Logger_t`)
race on formatting even though they're logically independent calls.

The cost is stack usage: `LOGGER_MAX_MESSAGE_LENGTH` bytes on the stack
for the duration of every `loggerWrite()` call (plus `vsnprintf`'s own
internal stack usage, which this module doesn't control). This is the
direct motivation for keeping `LOGGER_MAX_MESSAGE_LENGTH` modest
(Section 9) rather than generous.

## 9. Maximum Message Size / Truncation

```c
#ifndef LOGGER_MAX_MESSAGE_LENGTH
#define LOGGER_MAX_MESSAGE_LENGTH (128U)
#endif
```

Same override-friendly `#ifndef` pattern as `LOGGER_COMPILE_TIME_LEVEL`
(Section 4) -- a build can raise or lower it via `-D` without editing the
header. 128 bytes is a reasonable default for a single log line on a
small MCU without being so large it dominates every call's stack usage
(Section 8); the exact value is a tuning knob, not a load-bearing
decision, precisely because it's a build-time constant rather than
baked into the algorithm.

**Truncation is silent, and still reports `LOGGER_SUCCESS`, not a
distinct status.** `snprintf`/`vsnprintf` (C99) already guarantee the
buffer is null-terminated within its bounds and report how many bytes
*would* have been written if the buffer were unbounded -- this module
uses that return value only to clamp `length` (Section 7) to what
actually landed in `buffer`, never to write past `LOGGER_MAX_MESSAGE_LENGTH
- 1`. A truncated message was still logged, just shortened -- the logging
operation itself didn't fail, so signaling it as an error would
conflate "this succeeded, with a length caveat" with "this didn't
happen." Consistent with this project's general preference (established
across `crc16Verify`/`crc32Verify`'s mismatch-is-not-error precedent) not
to introduce a status code for an outcome that isn't actually a
malfunction.

**Concrete overflow-safety detail, since two separate pieces get written
into one buffer (the `[LEVEL][MODULE]` prefix from Section 14, then the
caller's formatted message):** both `snprintf` calls use the buffer's
*remaining* space, computed and clamped so the second call's size
argument can never underflow if the first call's return value already
met or exceeded the buffer size:

```c
int prefixLen = snprintf(buffer, sizeof(buffer), "[%s][%s] ", levelStr, module);
size_t used = (prefixLen > 0 && (size_t)prefixLen < sizeof(buffer)) ? (size_t)prefixLen : sizeof(buffer) - 1U;
/* remaining space computed from `used`, never from the raw (possibly
 * larger-than-buffer) prefixLen -- this specifically prevents a
 * size_t underflow (sizeof(buffer) - prefixLen going negative-then-huge
 * if prefixLen >= sizeof(buffer)) feeding an enormous size into the
 * second snprintf call. */
```

This is the same category of care CRC32's `validateBuffer` NULL/`len`
ordering required -- an easy-to-get-wrong detail worth locking in design
rather than discovering during implementation review.

## 10. Module/Source Identification

**Locked: a `const char *` string tag supplied at each call site, not a
central enum.** An enum (`LOGGER_MODULE_CRC32`, `LOGGER_MODULE_MEMORY_POOL`,
...) would require `logger.h` to know the name of every module that might
ever log through it -- meaning adding a new module anywhere in this
project would require editing `logger.h`, and `logger.h` would need to be
updated *before* a new module could even compile its own logging calls.
That inverts the dependency this project has been consistent about so
far: modules are independent of each other (`crc16`/`crc32`/`packetParser`/
etc. don't include one another's headers), and Logger shouldn't be the
first place that breaks. A string literal costs nothing at the call site
(stored in read-only memory, passed as a pointer) and needs zero
coordination with `logger.h` -- a module just passes its own name.

## 11. Return-Status Semantics

```c
typedef enum
{
    LOGGER_SUCCESS = 0,
    LOGGER_ERROR_NULL_POINTER,
    LOGGER_ERROR_NO_BACKEND,
    LOGGER_ERROR_INVALID_LEVEL
} LoggerStatus_t;
```

Matches the requirements-locked distinction exactly: a filtered call
(Section 3) returns `LOGGER_SUCCESS` without ever checking whether a
backend exists; an allowed call with `logger->backend == NULL` returns
`LOGGER_ERROR_NO_BACKEND`. `LOGGER_ERROR_INVALID_LEVEL` is the fourth
code, added per Section 3's validation.

**`loggerWrite()`'s full validation order, locked explicitly since the
order itself is a real decision, not just a list of checks:**

```
1. logger == NULL?              -> LOGGER_ERROR_NULL_POINTER
2. level not one of the four
   real severities?              -> LOGGER_ERROR_INVALID_LEVEL
3. level > runtimeThreshold?     -> LOGGER_SUCCESS (filtered; stop here --
                                     nothing below this point is checked)
4. module == NULL?               -> LOGGER_ERROR_NULL_POINTER
5. fmt == NULL?                  -> LOGGER_ERROR_NULL_POINTER
6. backend == NULL?              -> LOGGER_ERROR_NO_BACKEND
7. format, clamp, call backend   -> LOGGER_SUCCESS
```

Two things worth calling out about this order:

- **`module == NULL` and `fmt == NULL` are validated *after* filtering
  (step 3), not before.** This follows the same reasoning already locked
  for the no-backend case: a filtered-out call is defined to do no work
  beyond the threshold comparison itself, full stop -- extending that, a
  `LOGGER_DEBUG(logger, NULL, NULL)` call that gets filtered out by a
  `runtimeThreshold` of `LOGGER_LEVEL_WARN` returns `LOGGER_SUCCESS`
  without ever inspecting `module` or `fmt`, exactly as it would if they
  were valid. Validating them before filtering would mean a filtered
  call's return value depends on argument content that a filtered call
  is otherwise defined never to look at -- an inconsistency.
- **`module == NULL` and `fmt == NULL` both map to
  `LOGGER_ERROR_NULL_POINTER` -- deliberately not a substituted default**
  (e.g. `"UNKNOWN"` for a NULL module). Substitution would make the
  emitted message format (Section 14) depend on an implicit, undocumented
  fallback string, and would silently mask what is, in both cases, a
  caller programming error -- passing `NULL` where a real string was
  required, the same category of mistake `crc16`/`crc32`/every other
  module in this project already treats as a `NULL_POINTER` error rather
  than working around. `fmt == NULL` in particular must never reach
  `vsnprintf`, whose behavior with a `NULL` format string is not
  something this design relies on or wants to depend on.

**Important subtlety this design has to resolve, not just `loggerWrite()`
itself:** the `LOGGER_ERROR`/`WARN`/`INFO`/`DEBUG` *macros* do not reliably
have this return type. When compile-time-enabled, they expand to a call
to `loggerWrite()` and evaluate to a `LoggerStatus_t`; when
compile-time-disabled, they expand to `((void)0)` and evaluate to
`void`. Code that tried to do `LoggerStatus_t s = LOGGER_DEBUG(...)` would
compile in a debug build and fail to compile the moment `DEBUG` gets
excluded at compile time -- a latent, build-configuration-dependent
compile error waiting to happen. **Locked resolution: the `LOG_*` macros
are documented as statement-only** -- always used as a bare statement
(`LOGGER_ERROR(logger, "MOD", "message");`), never as an expression whose
value is assigned, compared, or passed anywhere. Callers that genuinely
need the status of a log attempt call `loggerWrite()` directly (which
always exists, always returns `LoggerStatus_t`, and does its own runtime
check regardless of the compile-time macros) instead of going through a
macro. This gets documented prominently in `logger.h`, not left implicit,
specifically because it's exactly the kind of thing that looks fine in
whatever build configuration a developer happens to be using locally and
breaks in a different one.

## 12. Reentrancy

- **Genuinely reentrant across independent `Logger_t` instances**, no
  qualification needed -- no global or static state anywhere in this
  module (Section 1, Section 8), so two contexts never interact.
- **Concurrent `loggerWrite()` calls into the *same* `Logger_t`
  instance**, which is the normal case (most programs share one logger),
  are safe from a formatting-corruption standpoint specifically because
  the buffer is stack-local (Section 8) -- there's no shared buffer for
  two concurrent calls to race on. They do both *read*
  `logger->runtimeThreshold` and `logger->backend`, which is safe as
  long as those reads aren't torn -- true in practice on typical
  32-bit-and-wider embedded targets for single, naturally-aligned
  `enum`/pointer-sized reads, though the C standard itself makes no
  atomicity guarantee here, so this isn't claimed as a formal guarantee,
  only a practical observation.
- **Concurrent `loggerSetLevel()`/`loggerSetBackend()` calls racing
  against `loggerWrite()` on the same context** are a genuine data race
  (one thread mutating `logger->backend` while another reads it) and are
  explicitly the caller's responsibility to serialize -- identical
  precedent to `ringBuffer`/`softwareTimer` pushing same-object
  concurrent-access synchronization onto the caller.
- **Per the requirements doc, this says nothing about ISR-safety.**
  `vsnprintf` in particular is not claimed safe to call from an
  interrupt context by this design -- reentrancy and ISR-safety remain
  the two separate claims requirements insisted they be treated as.

## 13. Initialization / Backend Registration / Clearing

```c
LoggerStatus_t loggerInit(Logger_t *logger, LoggerLevel_t initialThreshold);
LoggerStatus_t loggerSetBackend(Logger_t *logger, LoggerBackend_t backend, void *backendContext);
LoggerStatus_t loggerClearBackend(Logger_t *logger);
LoggerStatus_t loggerSetLevel(Logger_t *logger, LoggerLevel_t threshold);
LoggerStatus_t loggerGetLevel(const Logger_t *logger, LoggerLevel_t *outThreshold);
```

`loggerInit()` deliberately does **not** accept a backend parameter --
`logger->backend` is `NULL` immediately after `Init`, on purpose. This is
what makes "an allowed log call with no backend registered" a real,
reachable, testable state (Section 11's `LOGGER_ERROR_NO_BACKEND`) rather
than something that can only happen after an explicit `loggerClearBackend()`
call. Registration is a separate, explicit step
(`loggerSetBackend`), matching how `softwareTimerCreate()` requires a
non-NULL callback up front but keeps timer *creation* and timer
*starting* as separate steps -- initialization and "ready to actually do
the thing" aren't collapsed into one call here either.
`loggerClearBackend()` exists as its own function (rather than making
callers pass `NULL` to `loggerSetBackend`) for symmetry with `Init`'s
NULL-backend state and for clarity at call sites -- "stop logging
anywhere" reads more clearly as its own named operation than as
`loggerSetBackend(logger, NULL, NULL)`, though the two are equivalent in
effect.

## 14. Exact Message Format

```
[<LEVEL>][<MODULE>] <formatted caller message>
```

For example: `[ERROR][CRC32] checksum mismatch: expected 0x%08X, got
0x%08X` after `vsnprintf` fills in the arguments. `<LEVEL>` is rendered
as a fixed short string (`"ERROR"`, `"WARN"`, `"INFO"`, `"DEBUG"` -- an
internal lookup table indexed by `LoggerLevel_t`, not derived from the
enum's numeric value). This exact template is a comparatively low-stakes
decision relative to the rest of this document (unlike, say, the
final-XOR ordering in CRC32's design) -- it's easy to change later
without touching the module's behavior or API, since it only affects
what string the backend receives, not how filtering, buffering, or
status codes work. It's locked here mainly so the unit tests have a
concrete string to assert against, not because the template itself is
architecturally significant.

## 15. Configuration Macros

All project-wide Logger configuration is `#ifndef`-guarded in `logger.h`
so a build can override any of them with a `-D` flag, no header edits
required:

| Macro | Default | Purpose |
|---|---|---|
| `LOGGER_COMPILE_TIME_LEVEL` | `LOGGER_LEVEL_DEBUG_VAL` (everything compiled in) | Gates which `LOG_*` macros expand to real calls vs. `((void)0)` (Section 4) |
| `LOGGER_MAX_MESSAGE_LENGTH` | `128U` | Size of the stack-local formatting buffer (Sections 8-9) |

Plus the five `LOGGER_LEVEL_*_VAL` integer constants (Section 4), which
exist purely to make `LOGGER_COMPILE_TIME_LEVEL` comparable in `#if`
directives and are not meant to be independently configured.

## 16. Interaction With the Existing ECU Platform

**Out of scope for this design, by design:** wiring `crc16`, `crc32`,
`packetParser`, `memoryPool`, `ringBuffer`, or `softwareTimer` to
actually call into Logger. This document defines Logger itself, matching
requirements' framing of Logger as "the one other modules will
eventually call into" -- *eventually* is doing real work in that
sentence. Retrofitting existing, already-shipped, already-tested modules
to add logging calls is separate follow-up work with its own review, not
something to bundle into Logger's own milestone item.

**No circular or new coupling is introduced by Logger's existence.**
`logger.h` includes nothing from any other module in this project, and
(per Section 10's decision) no other module's header needs to include
`logger.h` either just to be *loggable* -- a module that later adds
logging calls needs only `#include <logger.h>` in its own `.c` file and
a `Logger_t*` passed in by whoever wires it up, not a header-level
dependency change to `logger.h` itself. This keeps the project's
existing per-module header independence intact.