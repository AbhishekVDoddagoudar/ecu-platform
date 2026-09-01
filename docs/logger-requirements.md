# Logger Requirements

## Locked Architectural Decisions

Logger has more genuine architectural forks than any prior Milestone 1
module, so — same approach as CRC32's parameter table — the decisions
that would otherwise get assumed silently during design are pinned here
first.

| Decision | Choice | Why |
|---|---|---|
| Output backend | **Pluggable**, via a caller-supplied output function pointer | Logger has no idea what "output" means on a real ECU -- UART, a debug console, a flash-backed trace buffer, or (for host-side unit tests) nothing at all. Hard-coding one destination would make the module untestable off-target and require an API-breaking change the day a second backend is needed. |
| Message formatting | **printf-style variadic** (`fmt, ...`) | Chosen over fixed/enum-based messages for flexibility, accepting the real costs that come with it: `vsnprintf`/variadic-argument overhead, and a formatting buffer that has to live *somewhere* (see below) rather than not existing at all. |
| Log level filtering | **Both** runtime (a checked threshold variable) *and* compile-time (macros that let below-threshold calls be excluded from the build entirely) | Runtime alone can't shrink the binary for levels a given build will never need (e.g. stripping `LOG_DEBUG` entirely from a release image); compile-time alone can't let a shipped/flashed binary change its verbosity without reflashing. Both together cover both use cases. Their relationship is pinned explicitly below rather than left as an open question. |

**Filter ordering (locked):** a log call must pass the compile-time
threshold, then the runtime threshold, before formatting or backend
invocation ever happens -- in that order:

```
LOG_DEBUG(...)
     |
     | compile-time DEBUG enabled?
    NO ──────────────> nothing (no code generated for this call)
    YES
     |
     | runtime threshold allows DEBUG?
    NO ──────────────> nothing (no formatting, no backend call)
    YES
     v
   format
     v
   backend
```

This ordering matters beyond documentation clarity: it's what lets a
compile-time-excluded call be excluded *before* the runtime check ever
exists in the generated code, rather than the two mechanisms competing to
decide the same thing.

**Compile-time exclusion (reworded, more defensible as a requirement):**
the earlier phrasing ("don't exist in the compiled binary at all") claims
more than this module can strictly guarantee, since the actual generated
code also depends on compiler optimization settings and inlining
decisions outside this module's control. The requirement is instead:

> Logging calls below the compile-time threshold shall be excluded from
> the logging execution path in the generated source -- the macro
> expansion itself must introduce no call to the formatting/backend path
> -- whenever the configured compile-time logging level excludes that
> call's severity.

Whether the compiler's dead-code elimination then also removes the
now-unreachable supporting code is a compiler/optimization-level detail,
not something this module's requirements can promise on their own.

## The Formatting-Buffer Question This Creates

Choosing printf-style variadic formatting is not free: something has to
hold the formatted string before it's handed to the output backend, and
per this project's coding guidelines ("minimize global variable usage,"
"encapsulate state within modules... prefer passing state through
function parameters"), that can't be a single shared static/global
scratch buffer -- two independent logging calls from genuinely
concurrent contexts (e.g. one from the main loop, one from code invoked
closer to an interrupt) would corrupt each other's in-flight message if
they shared one buffer. This requirements doc does not settle *where* that buffer
lives (a stack-local buffer inside the logging call is the obvious
candidate, but stack budget on small MCUs is a real constraint that
deserves its own design discussion) -- it only establishes that the
buffer's ownership/lifetime is a first-class design question, not an
incidental implementation detail to be discovered while writing
`logger.c`.

## What Problem Is This Solving?

Give every module in this platform (`crc16`, `crc32`, `packetParser`,
`memoryPool`, `softwareTimer`, and future ones) a single, consistent way
to report what's happening -- errors worth surfacing, warnings worth
noting, and diagnostic detail worth having during bring-up -- without
each module inventing its own ad-hoc `printf` calls, its own severity
convention, or its own way of being switched off for a release build.
This is the last module in Milestone 1 specifically because it's the one
other modules will actually call *into*, rather than being called by a
higher layer the way `crc16`/`crc32`/`memoryPool` are -- which is also
why its output-abstraction and reentrancy properties matter more than
they did for a self-contained checksum module.

## Responsibilities

Restating the GitHub issue's requirements list as concrete
responsibilities:

- Provide a small, fixed set of **severity levels** (error, warning,
  info, debug -- exact names/count settled in design) that every log call
  is tagged with.
- Let a log call identify **which module** it came from, so a log stream
  from multiple modules is still attributable at read time. The
  requirement is deliberately representation-agnostic: *"every log
  message shall carry a source/module identifier sufficient to identify
  its originating module"* -- whether that identifier is a string
  (`"CRC32"`), an enum (`LOGGER_MODULE_CRC32`), or something else is a
  design decision, not settled here.
- Support **formatted messages** (per the locked decision above,
  printf-style).
- Support a **configurable runtime threshold**: calls below the
  currently-configured level produce no output.
- Support **compile-time exclusion**, per the filter-ordering and
  reworded guarantee locked above.
- **Abstract the output destination** behind a caller-supplied function,
  so this module never itself calls `printf`, touches a UART register, or
  assumes any particular target.
- Provide a way to **suppress all output**. **Locked decision:** this is
  *not* a separate enable/disable flag alongside the runtime threshold --
  it is a dedicated `LOGGER_LEVEL_OFF` (naming TBD in design) value the
  runtime threshold can be set to, which suppresses every severity. A
  separate boolean would create two pieces of state that can disagree
  with each other (`enabled = false` with `threshold = DEBUG`, or
  `enabled = true` with `threshold = OFF` -- both ambiguous about which
  one wins). One threshold value, one meaning. If a genuinely independent
  hardware/master kill-switch for logging is needed later, that's new
  scope to be proposed explicitly, not something to fall out of this
  design by default.
- Be **unit-testable** without any real hardware backend -- a test
  harness must be able to register a backend that just captures output
  into a buffer for assertions, which the pluggable-backend decision
  already enables.

## Explicitly Out of Scope

- **Log storage/persistence.** This module hands formatted messages to a
  backend function and is done. Whether those messages end up on a UART,
  in flash, or nowhere is entirely the backend's concern, not this
  module's.
- **Log rotation, buffering/queuing, or batching.** No ring buffer of
  pending log messages is part of this module's responsibility (even
  though `ringBuffer` already exists in this project and could be a
  backend's internal implementation detail) -- Logger calls the backend
  function synchronously, once per log call, and has no notion of
  "later."
- **Timestamps.** Following the same precedent `softwareTimer` set for
  itself ("does not generate its own notion of time"), Logger does not
  source timestamps. If a caller wants a message timestamped, that's
  either baked into the format string by the caller, or a design-phase
  decision to accept an optional caller-supplied tick value -- not
  something this module invents on its own.
- **Any specific wire/text format for the emitted message** (e.g. a fixed
  `[LEVEL][MODULE] message` template) -- that's a design-phase decision,
  not a requirement.
- **Multi-line messages, log message IDs/codes, or structured
  (binary/JSON) logging.** The issue asks for formatted text messages;
  anything more structured than that is new scope, not implied by "log
  severity levels" and "formatted log messages."

## Inputs (at the requirements level -- exact signatures come in API design)

- A **severity level** for each log call.
- A **module/source identifier** for each log call.
- A **format string plus arguments**, per the locked printf-style decision.
- A **configurable runtime severity threshold**, maintained as part of
  the Logger's configuration/context state -- not a parameter passed to
  every individual log call. (`loggerWrite(level, ...)` reads the
  currently-configured threshold; it does not take a threshold argument
  itself.)
- A **backend function** the caller registers, which receives the
  formatted output.
- Some mechanism (macro-based, per the locked decision) for excluding
  calls below a compile-time threshold from the build.
- A **maximum formatted-message length**, fixed by a compile-time
  configuration constant (e.g. `LOGGER_MAX_MESSAGE_LENGTH`; exact name
  and value are design decisions). This is a hard requirement, not a
  design nicety -- see Guarantees.

## Outputs

- The formatted message, delivered to the registered backend function --
  this module produces no return value carrying the message itself; the
  backend is the only consumer of the actual text.
- A status/result indication for calls that can meaningfully fail (no
  backend registered, NULL format string, etc. -- exact set defined in
  design, following this project's `_Status_t` enum convention).

## Failure Cases

Working through this from scratch, the way CRC32's and the memory pool's
requirements did:

1. **Locked:** a valid, allowed log call (passes both the compile-time
   and runtime thresholds) with no backend currently registered is a
   genuine configuration failure, reported as
   `LOGGER_ERROR_NO_BACKEND` (naming TBD) -- *not* silently swallowed.
   This is distinct from case 2 below: a call *filtered out* by the
   runtime threshold never gets far enough to check whether a backend
   exists, and reports success. The distinction matters because
   "filtered" is routine operation, while "allowed, but nowhere to go" is
   a real misconfiguration (e.g. a missing initialization step) worth
   being able to detect:

   ```
   DEBUG filtered by runtime threshold
        -> no formatting attempted, no backend lookup, LOGGER_SUCCESS

   INFO allowed by both thresholds, backend == NULL
        -> LOGGER_ERROR_NO_BACKEND
   ```
2. A log call at a severity below the current runtime threshold (or
   excluded entirely at compile time). This is expected, routine
   behavior (like `crc16Verify`'s mismatch), not a malfunction --
   reported as `LOGGER_SUCCESS`, per the distinction in case 1.
3. A log call whose format string references arguments that don't match
   what was actually passed (a classic printf-family footgun). This
   module cannot detect this at runtime in portable C; the design/API
   docs should say so plainly rather than implying a safety net that
   doesn't exist. Compiler format-string-checking attributes
   (`__attribute__((format(printf, ...)))` on GCC/Clang) are worth
   considering in design as a *build-time*, not runtime, mitigation.
4. A formatted message longer than `LOGGER_MAX_MESSAGE_LENGTH` (the
   compile-time bound locked in Inputs, above). **Locked as a hard
   requirement:** this module must never write past that fixed buffer's
   end for an oversized message, full stop. The *specific* behavior on
   overflow (silent truncation, a distinct status code, or something
   else) remains a design decision -- only the existence of the bound and
   the ban on ever exceeding it are locked here.
5. NULL format string, or NULL backend function pointer at registration
   time -- straightforward NULL-pointer cases, same shape as every prior
   module.
6. Compile-time-excluded calls and runtime-filtered calls must not be
   observably different to correct caller code other than "produces no
   output" -- e.g. a compiled-out `LOG_DEBUG(...)` call must still be
   syntactically valid. **Locked, stated as its own explicit
   requirement:** when a log call is excluded at compile time, its
   arguments shall not be evaluated. Concretely,
   `LOG_DEBUG("value = %d", expensiveFunction())` with `DEBUG` compiled
   out must not execute `expensiveFunction()` -- a compiled-out call must
   not silently run side-effecting expressions a caller placed in its
   arguments. This gives design and the eventual test suite a precise,
   checkable criterion, rather than "probably shouldn't run the
   arguments" left implicit.

## Guarantees

- **No mandatory dynamic memory allocation** -- per the issue's explicit
  requirement. Given the locked printf-style decision, this means the
  formatting buffer is some fixed-size, non-heap storage; where exactly
  it lives is a design question (see "formatting-buffer question" above),
  but "no heap" is non-negotiable regardless of where design lands.
- **Bounded, non-dynamic storage and operations.** Beyond just "no heap
  allocation": Logger shall use only bounded, fixed-size storage and
  shall not perform unbounded memory operations of any kind (e.g. no
  unbounded string operations that don't respect
  `LOGGER_MAX_MESSAGE_LENGTH`). This complements, rather than duplicates,
  the no-dynamic-allocation requirement above -- it's possible to avoid
  the heap while still writing unbounded-length data into a fixed buffer
  incorrectly, and this guarantee rules that out too.
- **Reentrant, where applicable.** Sharpened as an explicit requirement,
  not just a design preference: *"Logger shall not use shared mutable
  formatting state between independent logging calls."* Whether design
  satisfies this with a stack-local buffer per call, a caller-provided
  buffer, or another bounded approach is open -- but no shared/global
  scratch state for formatting is not open. Separately, whatever the
  *backend* function does with the already-formatted output (writing to
  a single shared UART, for instance) is the backend's own
  synchronization responsibility, the same way `ringBuffer`/
  `softwareTimer` push concurrent-access synchronization to the caller
  for their own shared resources.
- **Reentrancy is not the same claim as ISR-safety.** This is worth
  stating explicitly rather than leaving implicit: being reentrant
  (safe to call concurrently from independent, non-interrupt execution
  contexts) does not mean this module is safe to call from within an
  interrupt service routine. `vsnprintf`-based formatting in particular
  may be unsuitable for some ISR contexts on constraints (execution time,
  stack depth, or implementation-specific behavior) this project hasn't
  evaluated. **ISR usage is explicitly out of scope for this module's
  initial requirements** unless a future design revision deliberately
  takes it on -- callers should not assume ISR-safety just because the
  module is reentrant.
- **No unnecessary global mutable state** -- per the issue's explicit
  acceptance criterion. The registered backend function pointer and the
  current runtime threshold are the two pieces of state this module
  plausibly needs to remember between calls; whether those live in a
  caller-owned context struct (matching every other module in this
  project) or as module-internal state is a design decision, but
  "caller-owned struct, no library-global state" is the strong default
  given every other module in this project already follows that pattern.

## Limitations Callers Should Understand

- This module does not know or care what happens to a message after
  handing it to the backend -- if the backend drops it, blocks
  indefinitely, or corrupts it, that's entirely outside this module's
  visibility or control.
- No timestamping, no persistence, no log rotation -- see Explicitly Out
  of Scope.
- No independent master enable/disable switch -- suppression is entirely
  through the runtime threshold reaching `LOGGER_LEVEL_OFF`. If a
  hardware-level kill-switch is needed later, it's new scope.
- No ISR-safety guarantee. Reentrancy (safe concurrent use from
  independent non-interrupt contexts) is required; safety when called
  from an interrupt service routine is explicitly not, unless a later
  design revision takes it on deliberately.
- Variadic-argument/format-string mismatches are a caller responsibility
  this module cannot catch at runtime.
- Messages longer than `LOGGER_MAX_MESSAGE_LENGTH` are bounded by
  design -- this module will never write past that limit -- but the
  precise behavior for an oversized message (truncation, a distinct
  status, etc.) is a design decision, not settled here.