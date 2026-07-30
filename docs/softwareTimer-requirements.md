# Software Timer Requirements
This covers Responsibilities, Inputs, Outputs, Error Conditions, and Timer
Behaviour only, as agreed. States, Memory Ownership, Tick Source,
Complexity, and Limitations are deliberately not attempted yet.

## 1. Responsibilities

A software timer's job is to count elapsed ticks on behalf of the
application and signal — via a callback — the moment a requested count of
ticks has passed. It supports two shapes of that signal: fire once
(one-shot) or fire repeatedly at the same interval (periodic). That's the
entire job: count, and notify at zero.

It should **not**:
- Generate its own notion of time. It has no relationship to hardware
  timers, interrupts, or any clock source — it only knows "one tick just
  happened" when something else tells it so.
- Sleep, block, or delay execution in any way. This is a bookkeeping
  module, not a scheduler — it doesn't pause anything, it just counts.
- Know what a tick *means* in real time (1 ms? 10 ms? 100 µs?). That
  interpretation belongs entirely to whatever external code decides when
  to report a tick. The module only ever counts abstract ticks.
- Create threads, tasks, or know anything about an RTOS. Whatever
  concurrency model surrounds this module is external to it.
- Allocate memory dynamically. Whatever storage backs a set of timers has
  to be fixed and caller/compile-time determined, not grown at runtime.
- Arbitrate importance between timers. There's no priority, preemption, or
  ordering guarantee implied between two timers that happen to expire "at
  the same time" (i.e. as a result of the same tick call) beyond whatever
  order they happen to be processed in internally.

If I find myself wanting this module to also decide *when* to check for
expiry, or to manage its own clock, that's a sign responsibility has
crept beyond "count ticks, notify at zero."

## 2. Inputs

Thinking about this operation by operation, since a stateful timer module
has several distinct entry points rather than one call like the packet
parser:

- **To bring a timer into existence**: a requested duration in ticks, a
  callback to invoke at expiry, an optional argument/context to pass to
  that callback, and an indication of one-shot vs. periodic mode. Some way
  to later refer back to *this specific* timer (not designing what that
  reference looks like yet — just noting the need for one).
- **To start (or restart) a timer**: which timer, and a duration in ticks
  (whether this is required again here, or only supplied once at creation,
  is a question I'm leaving open for the design phase).
- **To stop a timer**: which timer.
- **To advance time**: nothing beyond the call itself. The very act of
  invoking the tick operation *is* the input — each call means "exactly
  one tick has elapsed, system-wide," not "N ticks have elapsed." Whatever
  external code decides to call this every 1 ms, every 10 ms, or on every
  hardware timer interrupt is a decision this module has no visibility
  into (this connects to the Tick Source topic explicitly deferred to a
  later section, but it directly shapes what "input" means for this one
  operation, so it's worth stating here).
- **To query a timer**: which timer, when asking "is it running," "has it
  expired," or "how many ticks are left."

## 3. Outputs

- **From creation**: something the caller can use to refer to this timer
  in every subsequent call (start/stop/query) — not designing its shape
  yet. Also, a result indicating success or a specific reason it couldn't
  be created (see Error Conditions).
- **From start/stop**: a result indicating success or a specific failure
  reason — consistent with every other module in this project so far,
  nothing should silently no-op.
- **From advancing time**: primarily a side effect (callbacks firing for
  any timer that just reached zero), not a return value in the way
  start/stop have one. Whether the tick operation should *additionally*
  report something back to its caller — e.g. "did anything expire this
  tick," "how many timers expired" — is an open question I'm not
  resolving now; it's not obviously required by the core responsibility,
  but it might be useful for diagnostics. Flagging it rather than deciding
  it.
- **From querying a timer**: its current state (conceptually — running,
  stopped, expired; the actual state model is Section 6, not today), and
  possibly how many ticks remain before it would next expire.

Across all of these: every operation that can fail returns a distinguishable
result rather than silently doing nothing, matching bitUtils / crc16 /
ringBuffer / packetParser.

## 4. Error Conditions

Trying to think of these from scratch rather than by analogy to a specific
existing timer library:

1. A required callback is NULL.
2. A required timer reference/handle is NULL (if the reference ends up
   being pointer-shaped — not decided yet).
3. No space left to create a new timer (the pool, however sized, is full).
4. An operation refers to a timer reference that was never created, or no
   longer refers to anything valid.
5. `start()` is called on a timer that's already running. (Or — is that
   actually wrong? A restart-on-demand pattern, like a debounce or
   watchdog timer, is a completely legitimate use case where calling
   start() on an already-running timer *should* just reset its countdown
   rather than be rejected. I'm genuinely unsure which behavior is
   correct here, and I don't think I should decide it just by listing it
   as an "error" — flagging this as a real open question for the design
   phase rather than quietly assuming rejection is correct.)
6. `stop()` is called on a timer that isn't running (already stopped, or
   never started).
7. A requested timeout of 0 ticks. Ambiguous on its face — does that mean
   "expire immediately," "expire on the very next tick," or is it simply
   disallowed? Not resolving the exact semantics here, just flagging that
   0 needs an explicit answer, not an implicit one.
8. An operation is attempted on a timer slot that was never initialized /
   has been reset back to unused.
9. An invalid mode value is supplied (something that's neither one-shot
   nor periodic).
10. A stale reference is reused — referring to a timer that was deleted
    (or expired and reset) and whose underlying slot has since been
    reused for a completely different timer. This is the "use-after-free"
    class of bug at the API level, not the memory level.
11. The tick-advance operation is somehow called in a way that implies
    more or less than exactly one tick (only relevant if its contract
    ever allows a count parameter — see Inputs — but worth naming as a
    condition in case that design direction is taken).
12. An attempt to create or operate on any timer before the module itself
    has been brought into a valid initial state (if an init step turns
    out to be necessary at all — not decided).
13. Two operations on the same timer reference happening from what the
    caller believes are two independent, unsynchronized contexts (e.g.
    calling stop() on a timer from one context while tick() is
    simultaneously processing it from another) — not a "bug in the
    module" exactly, but a condition whose safety (or lack of it) needs
    to be explicitly stated rather than left implicit, the same way
    reentrancy/thread-safety has been documented explicitly in every
    prior module.
14. A callback itself tries to start, stop, or otherwise operate on
    timers (including possibly itself) from within its own invocation.
    Not obviously an "error" — it might be a perfectly valid and useful
    pattern — but it's a condition worth naming now because *something*
    has to be true about whether that's safe, and I don't think it's
    safe to assume it "just works" without deciding.

That's 14 — a few of these (5, 7, 14) I'm deliberately presenting as open
questions rather than settled error conditions, because I don't think they
can be answered correctly without more thought, and I'd rather flag the
ambiguity than quietly bake in an answer that turns out to be wrong.

## 5. Timer Behaviour

This is the section I want to be most precise about, since ambiguity here
would make every other decision downstream shaky.

**Walking through the example timeline literally:**

```
tick 0:  create(timeout = 5)
         start()
tick 1:  tick() called  → remaining: 5 → 4
tick 2:  tick() called  → remaining: 4 → 3
tick 3:  tick() called  → remaining: 3 → 2
tick 4:  tick() called  → remaining: 2 → 1
tick 5:  tick() called  → remaining: 1 → 0  → callback fires, during this call
```

**The rule I'm committing to:** starting a timer with a timeout of N sets
its remaining count to N. Each call to the tick-advance operation
decrements the remaining count of every currently-running timer by
exactly 1. The moment a timer's remaining count reaches 0 *as a direct
result of that decrement*, its callback fires — synchronously, within
that same call to the tick-advance operation. It does not wait for a
subsequent tick, and it does not fire "before" the decrement — the
decrement and the zero-check are the same event, not two separate ones.
This means "expire after N ticks" means exactly N calls to the
tick-advance operation must occur after start() before the callback
fires — matching the walkthrough above, where `start(5)` fires on the
5th subsequent tick call, not the 4th or 6th.

**All running timers advance together, not one at a time.** Since there is
exactly one shared time base (one call = one tick, system-wide, per
Responsibilities/Inputs), a single call to the tick-advance operation must
decrement *every* currently-running timer by one, not just one timer per
call. If three timers are running and one tick call happens, all three
lose exactly one from their remaining count in that same call.

**One-shot vs. periodic — the distinction I'm confident about:** a
one-shot timer fires its callback exactly once and then becomes inactive
(it does not keep counting down into negative territory or fire again). A
periodic timer is expected to keep firing at the same interval without
requiring the caller to call start() again each time.

**What I'm not deciding here:** the exact mechanics of a periodic timer's
reload — whether the remaining count resets to N in the same tick call the
callback fires in (so it's immediately eligible to count down again), or
on some later call; whether a callback is allowed to safely stop or
restart the very timer that's currently invoking it; and whether the
already-running-timer question from Error Conditions (#5) should be
resolved as "error" or "implicit restart." These feel like design-phase
questions, not requirements-phase ones — I can describe *that* periodic
timers repeat and *that* something must be true about callback-reentrancy
safety, without yet committing to *how*.

**Concurrency assumption underlying all of the above:** this description
assumes a single-threaded, sequential-call model — tick(), start(), and
stop() are assumed not to be invoked concurrently from independent
execution contexts unless the caller provides its own synchronization.
That's consistent with how "reentrant" has been defined in every prior
module in this project (safe for independent callers using independent
state, not internally lock-protected against concurrent access to the
*same* state).