# Software Timer High-Level Design

This document resolves every question left open in
`softwareTimer-requirements.md`, plus the additional design decisions
flagged during requirements review. `softwareTimer.h` is not written yet —
this is the "how," ahead of the API declarations.

## 1. Resolved Requirements Questions

Five things were explicitly left open in requirements; each is settled here
with the reviewer's recommended answer, adopted as-is because the
reasoning given for each is sound:

| # | Question | Resolution |
|---|---|---|
| 1 | Does `start()` on an already-running timer error, or restart? | **Restarts.** No error. `start()` always (re)arms the timer, regardless of prior state (except an invalid/unused handle, which is still an error). This is the debounce/watchdog/inactivity-timeout pattern — restart-on-demand is the common case, not an edge case. |
| 2 | Timeout of 0 ticks | **Rejected.** `SOFTWARE_TIMER_ERROR_INVALID_TIMEOUT`. Immediate expiry is ambiguous; a caller wanting "fire very soon" uses 1 tick explicitly. |
| 3 | Does the callback run inside `tick()`? | **Yes.** Synchronous, deterministic, no queue, no deferred-execution machinery. |
| 4 | Can a callback manipulate timers (including itself)? | **Allowed.** A callback may call start/stop (on itself or any other timer) from within its own invocation. See Section 4 for why reload-before-callback ordering is what makes this safe. |
| 5 | Pool exhaustion | **Confirmed as a real error**, `SOFTWARE_TIMER_ERROR_POOL_FULL`. Not solved by growing the pool — no dynamic allocation — so a full pool is a legitimate, expected runtime condition the caller must handle (e.g. reuse/delete an existing timer, or size the pool larger up front). |

## 2. Architectural Fork: Storage Model (flagging this as my call)

The requirements-review list referenced `#define SOFTWARE_TIMER_MAX_TIMERS`
as an example, implying a single, library-internal, compile-time-fixed
pool. I'm not adopting that — recommending the caller-owned-pool model
instead, the same shape as `ringBufferInit(ringBuffer, storage, capacity,
elementSize)`:

```c
SoftwareTimerStatus_t softwareTimerInit(SoftwareTimerPool_t *pool,
                                         TimerControlBlock_t *storage,
                                         size_t capacity);
```

The caller declares their own fixed-size array
(`TimerControlBlock_t myTimers[8];`) and hands it to `Init`. This is still
a fixed size with zero dynamic allocation — the fixedness just lives in the
caller's declaration rather than a single library-wide macro.

**Why I'm recommending this over a single global pool:**
- Every prior module in this project (bitUtils, crc16, ringBuffer,
  packetParser) is documented as reentrant with *no internal or global
  state*. A single global timer pool would be the first module to break
  that pattern, and I don't think there's a strong enough reason to.
- It allows more than one independent pool to coexist (e.g. a UI subsystem
  with a small pool of debounce timers, and a comms subsystem with its own
  pool of protocol timeouts) without them competing for the same fixed
  slot budget.
- It costs nothing extra — the caller was always going to need to decide a
  pool size; this just makes that decision explicit and local instead of a
  single project-wide constant.

**This is a genuine fork, not an obviously-correct call** — a single
global pool is simpler to reason about for a project with only one timer
subsystem, and avoids passing a pool pointer to every single API call.
Flagging it explicitly rather than deciding silently.

## 3. TimerHandle_t and Stale-Handle Safety

A pure array-index handle (`typedef uint8_t TimerHandle_t;`) can't detect
this bug: timer A is deleted, its slot is reused for timer B, and code
still holding A's old handle calls `start()` — which will silently operate
on B instead of erroring, because the index is still "valid," just
pointing at a different timer now. This is requirements error condition
#10 (stale handle reuse) and it deserves an actual answer, not a footnote.

**Decision: `TimerHandle_t` carries a generation counter alongside the
index.**

```c
typedef struct
{
    uint8_t index;
    uint8_t generation;
} TimerHandle_t;
```

Each pool slot stores its own generation counter, incremented every time
that slot is deleted. A handle is only valid if its `generation` matches
the slot's *current* generation — if timer A's handle has generation 3 but
the slot has moved on to generation 4 (because it was deleted and reused
as timer B), any operation using A's handle returns
`SOFTWARE_TIMER_ERROR_INVALID_HANDLE` instead of silently touching B.

**Cost:** the handle is 2 bytes instead of 1, and each slot needs one extra
byte for its generation counter. I think this is worth it — this project
has consistently favored catching a bug via an explicit status code over
staying silent, and stale-handle reuse is exactly the kind of intermittent,
hard-to-reproduce bug that favors erring toward detection. Flagging the
cost explicitly since it is a real tradeoff, not a free win.

## 4. Timer States and the Reload-Before-Callback Decision

**Three states, not four:**

```
UNUSED  →  STOPPED  ⇄  RUNNING
```

- `UNUSED`: slot not currently allocated to any timer.
- `STOPPED`: a timer exists here (created), but isn't counting down —
  either never started, explicitly stopped, or (for a one-shot timer)
  already fired once.
- `RUNNING`: actively counting down.

The original requirements sketch offered a fourth state, `EXPIRED`. I'm not
including it: because the callback fires synchronously inside `tick()`
(Section 1, #3), by the time `tick()` returns, a one-shot timer has already
transitioned to `STOPPED` and a periodic timer has already transitioned
back to `RUNNING` (reloaded). There's no moment where "expired" is a state
a subsequent query call could ever observe — it's an instantaneous
transition within `tick()`, not a resting state. Adding a state nothing can
ever observe just adds a branch nobody can reach.

**Reload happens *before* the callback fires, not after — and this is
directly informed by decision #4 in Section 1 (callback may manipulate
timers).** Consider a periodic timer whose callback wants to say "fire 5
more times, then stop yourself":

- **If reload happened after the callback**: the callback calls `stop()`
  on itself, but then the module's own post-callback reload logic runs and
  re-arms it anyway, silently undoing what the callback just did.
- **If reload happens before the callback** (the chosen order): the timer
  is already back in `RUNNING` with `remaining = timeout` *before* the
  callback is invoked, so if the callback calls `stop()`, that call is the
  last word — the timer ends up `STOPPED` as intended.

This ordering is what makes "callback safely controls its own timer's
future" actually work, rather than being a coin-flip depending on internal
implementation order.

## 5. TimerControlBlock_t Layout

```c
typedef struct
{
    SoftwareTimerState_t state;
    SoftwareTimerMode_t mode;
    uint32_t timeout;           /* original duration, in ticks; needed to reload periodic timers */
    uint32_t remaining;         /* ticks left before next expiry */
    SoftwareTimerCallback_t callback;
    void *context;
    uint8_t generation;
} TimerControlBlock_t;
```

`timeout` (the original duration) is kept separate from `remaining` (the
live countdown) specifically because periodic reload needs to know what to
reset `remaining` back to — collapsing them into one field would lose that
information the moment the first tick decrements it.

`uint32_t` for `timeout`/`remaining`: chosen over `uint16_t` to avoid
capping any single timer's duration at 65535 ticks, which could be a real
limitation depending on tick rate (e.g. a 1 ms tick would cap a `uint16_t`
timer at ~65 seconds). Costs 2 extra bytes per slot; worth it for the
range.

## 6. Callback Signature

```c
typedef void (*SoftwareTimerCallback_t)(TimerHandle_t handle, void *context);
```

Passes both the handle (so one callback function can serve multiple
timers and tell them apart) and the context pointer supplied at creation
(for caller-defined data). This combines the two options the requirements
draft left open rather than picking one — there's no real tradeoff here,
since carrying both costs nothing extra over carrying just one.

## 7. Create vs. Start — Splitting "Identity" from "Countdown"

Decision: `Create` and `Start` stay separate calls, and `Create` does
**not** take a timeout.

```c
SoftwareTimerStatus_t softwareTimerCreate(SoftwareTimerPool_t *pool,
                                           SoftwareTimerMode_t mode,
                                           SoftwareTimerCallback_t callback,
                                           void *context,
                                           TimerHandle_t *outHandle);

SoftwareTimerStatus_t softwareTimerStart(SoftwareTimerPool_t *pool,
                                          TimerHandle_t handle,
                                          uint32_t timeoutTicks);
```

`Create` sets up the things that describe *what this timer is* (its mode,
callback, context) and leaves it `STOPPED`, not counting. `Start` sets up
*the current countdown* (the timeout) and is the only operation that
actually begins ticking. This directly answers the open requirements
question "does start() receive the timeout again" — yes, every time,
because `Start` is the (re)arm operation and the same timer can legitimately
be armed for different durations at different points in its life (a
debounce timer that's sometimes 50 ticks and sometimes 100, depending on
context, without needing to delete and recreate it).

## 8. Stop Semantics — Idempotent, Not Strict

Calling `stop()` on an already-`STOPPED` timer is **not** an error — it's a
harmless no-op that still returns `SOFTWARE_TIMER_SUCCESS`. This mirrors
the restart-is-allowed reasoning for `start()` (Section 1, #1): the
category of "operation performed on a valid timer that happens to already
be in that state" is fundamentally different from "operation performed on
an invalid/nonexistent handle," and only the second one is a real error
(`SOFTWARE_TIMER_ERROR_INVALID_HANDLE`).

## 9. Delete

```c
SoftwareTimerStatus_t softwareTimerDelete(SoftwareTimerPool_t *pool, TimerHandle_t handle);
```

Not explicitly named in the original operation list, but implied by
requirements error condition #10 (stale handle reuse) — there has to be a
way to release a slot. `Delete` implies `stop` (any state → `UNUSED`
directly) and bumps the slot's generation counter, which is what makes
Section 3's stale-handle detection actually work.

## 10. Complexity

| Operation | Complexity | Why |
|---|---|---|
| `Init` | O(capacity) | Marks every slot `UNUSED` once. |
| `Create` | O(capacity) | Linear scan for a free (`UNUSED`) slot. No free-list in this version — simplest correct approach; revisit only if profiling shows pool-full scanning is a bottleneck. |
| `Start` | O(1) | Handle directly indexes the slot. |
| `Stop` | O(1) | Same. |
| `Delete` | O(1) | Same. |
| `Tick` | O(capacity) | Must inspect every slot to decrement any that are `RUNNING`. |

This matches what the original requirements sketch anticipated
(Create O(n), Tick O(number of timers)) — confirming rather than
revising those expectations.

## 11. Callback Ordering (multiple timers expiring on the same tick)

**Decision: ascending slot index order.** If timers in slots 2, 5, and 6
all reach zero as a result of the same `tick()` call, their callbacks fire
in that order — 2, then 5, then 6 — because `tick()` scans the pool array
front-to-back and fires each callback as it's encountered during that
single linear pass. No separate creation-order bookkeeping is needed; this
falls directly out of the O(capacity) scan already required. Documenting
this explicitly (per the reviewer's "any answer is acceptable, just
document it") — a caller relying on relative ordering between two
same-tick timers can now rely on slot index, though in general, minimizing
reliance on this ordering at the call-site level is still better practice.

## 12. Timer Accuracy

A timer with timeout N expires after exactly N calls to
`softwareTimerTick()` — no drift is introduced by this module itself.
Overall real-time accuracy depends entirely on the regularity of whatever
external code is calling `tick()` (a jittery caller produces a jittery
timer; this module cannot correct for that, since it has no notion of
real time at all — see Responsibilities).

## 13. Public API (preview — full doxygen comes with softwareTimer.h)

```c
SoftwareTimerStatus_t softwareTimerInit(SoftwareTimerPool_t *pool, TimerControlBlock_t *storage, size_t capacity);
SoftwareTimerStatus_t softwareTimerCreate(SoftwareTimerPool_t *pool, SoftwareTimerMode_t mode, SoftwareTimerCallback_t callback, void *context, TimerHandle_t *outHandle);
SoftwareTimerStatus_t softwareTimerStart(SoftwareTimerPool_t *pool, TimerHandle_t handle, uint32_t timeoutTicks);
SoftwareTimerStatus_t softwareTimerStop(SoftwareTimerPool_t *pool, TimerHandle_t handle);
SoftwareTimerStatus_t softwareTimerDelete(SoftwareTimerPool_t *pool, TimerHandle_t handle);
SoftwareTimerStatus_t softwareTimerTick(SoftwareTimerPool_t *pool);
SoftwareTimerStatus_t softwareTimerGetState(const SoftwareTimerPool_t *pool, TimerHandle_t handle, SoftwareTimerState_t *outState);
SoftwareTimerStatus_t softwareTimerGetRemainingTicks(const SoftwareTimerPool_t *pool, TimerHandle_t handle, uint32_t *outRemaining);
```

Eight functions. No separate `IsRunning()` convenience wrapper —
`GetState()` already answers that and any other state question, and I'd
rather keep the surface tight than add a wrapper whose only job is
comparing a state enum to one value (consistent with the "not too many,
not too few" pattern from every prior module's error-enum sizing).

`softwareTimerTick()` returns a status (e.g. `NULL_POINTER` if `pool` is
NULL) but deliberately does **not** report how many timers expired or
which ones — that was flagged as an open question in requirements
(Section 3) and I'm resolving it now as: not in this version. Adding it
speculatively risks the same problem packetParser's requirements analysis
flagged about premature extensibility hooks — easy to add later if a real
caller need shows up, awkward to remove if it turns out nobody uses it.

## 14. Status Codes

```c
typedef enum
{
    SOFTWARE_TIMER_SUCCESS = 0,
    SOFTWARE_TIMER_ERROR_NULL_POINTER,
    SOFTWARE_TIMER_ERROR_INVALID_HANDLE,
    SOFTWARE_TIMER_ERROR_INVALID_TIMEOUT,
    SOFTWARE_TIMER_ERROR_INVALID_MODE,
    SOFTWARE_TIMER_ERROR_POOL_FULL
} SoftwareTimerStatus_t;
```

Six codes. `INVALID_HANDLE` covers both "never created" and "stale
generation" (Section 3) as one category — a caller doesn't need to
distinguish those two cases differently, both mean "this handle doesn't
refer to a live timer right now."

## 15. Limitations

- Fixed-capacity pool per instance, sized by the caller at `Init` — no
  growth, no dynamic allocation.
- No internal synchronization — concurrent calls into the same pool from
  independent execution contexts (e.g. `stop()` from an ISR while `tick()`
  runs in the main loop) require the caller's own synchronization, per the
  reentrancy model established requirements Section 5.
- `Create` is O(capacity) due to linear free-slot scanning; not a concern
  for the pool sizes this module targets, but worth knowing if capacity
  ever grows very large.
- No priority or ordering guarantee between timers beyond the documented
  slot-index tie-break for same-tick expiry (Section 11).