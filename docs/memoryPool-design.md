# Memory Pool Design

This document resolves the open design questions from the requirements
phase with concrete decisions and the reasoning behind each one.
`memoryPool.h` is not written yet -- this is the "how," ahead of the API
declarations.

## 1. Problem Statement

The pool is a **runtime-configurable, fixed-size** memory pool: `blockSize`
and `blockCount` are chosen by the caller at initialization (so the same
module serves a pool of 16 32-byte blocks just as well as 8 128-byte
blocks), but once initialization succeeds, that configuration is fixed
for the pool's lifetime -- no growth, no shrinking, no changing block
size. It manages a caller-owned block of memory, divided into that fixed
size and count of blocks, and hands blocks out and takes them back in
bounded time -- no dynamic allocation, no external fragmentation by
construction, explicit detection of exhaustion and certain classes of
misuse (foreign pointer, already-free block). This document decides the
internal shape: how blocks are laid out, how free/used state is tracked,
and how a raw pointer handed back at `release` is validated before it's
trusted.

## 2. Design Fork: Bookkeeping Strategy

Two genuinely competing designs were considered for how the pool tracks
which blocks are free and detects misuse, and both forks are worth
recording explicitly rather than presenting the final choice as the only
option that was ever on the table.

**Fork A -- handle with a generation counter, found via linear scan.**
Instead of handing back a raw pointer, `acquire` could return an opaque
handle (an index plus a small counter). Each slot's counter increments
every time it's released; a handle carrying an old counter value is
rejected as stale. Finding a free slot is a linear scan over all blocks
looking for one marked free. This gives genuinely complete misuse
detection -- a handle from before a block was released and reused is
always caught, not just "usually" -- at two costs: the caller no longer
gets a directly-usable pointer from `acquire` (a handle has to be resolved
to an address separately, or every operation does that resolution
internally), and the scan's cost grows with pool size instead of staying
constant.

**Fork B (chosen) -- raw pointer, found via an explicit free stack.** The
caller gets back a real pointer they can write into immediately.
Finding/returning a free block is O(1) via a free stack instead of a scan.
The cost: without a generation counter, the pool cannot always tell a true
double free apart from a pointer that happens to pass all its validity
checks but was never actually handed out by `acquire` in the first place
-- both cases look identical from the pool's point of view (see Section
6).

**Why B was chosen:** the caller-ergonomics win (a pointer usable
immediately, no separate resolution step) and the O(1) guarantee were
judged more valuable than closing that last sliver of double-free
detection, especially since blocks here are expected to be
acquired/released frequently (e.g. once per message, once real consumers
of this pool exist) rather than the much lower frequency a one-time
"create this long-lived object" call would see. That frequency
consideration is also why the free stack won over a linear scan on its
own merits, independent of the handle question -- a scan's cost growing
with pool size is a real problem for something called this often, in a
way it would not be for something set up once and used for a long time.

Both are documented here as **genuine forks, not obviously-correct
calls** -- a future reader should be able to see that the raw-pointer,
free-stack design was a deliberate trade, not an oversight of the
alternative.

## 3. Block Storage Layout

Block storage is one contiguous, caller-owned array of `blockSize *
blockCount` bytes. Block `i` (0-indexed) always lives at:

```
address(i) = storage + (i * blockSize)
```

```
storage
  |
  v
+----------+----------+----------+----------+----------+
|  block 0 |  block 1 |  block 2 |  block 3 |  block 4 |
+----------+----------+----------+----------+----------+
```

No headers, canaries, or metadata live inside a block -- a block is
exactly `blockSize` bytes, and the pool never reads or writes block
contents on the caller's behalf. What the caller stores in an acquired
block is entirely their business. Block `i`'s address is computed, never
stored -- there is no per-block pointer table, only the arithmetic above
and its inverse (Section 6).

## 4. Pool Data Structures

Three caller-owned regions, tied together by one small descriptor. Block
state is expressed through an explicit enum so its symbolic meaning is
pinned down now, independently of whatever storage type ends up carrying
it:

```c
typedef enum
{
    MEMORY_POOL_BLOCK_FREE = 0,
    MEMORY_POOL_BLOCK_ALLOCATED
} MemoryPoolBlockState_t;

typedef struct
{
    uint8_t *storage;      /* caller-owned block storage: blockSize * blockCount bytes */
    uint8_t *state;        /* caller-owned, blockCount entries -- values are MemoryPoolBlockState_t, stored as uint8_t to keep metadata size minimal */
    size_t  *freeStack;     /* caller-owned, blockCount entries: LIFO stack of free block indices */
    size_t   blockSize;
    size_t   blockCount;
    size_t   freeCount;     /* also the current stack height */
    bool     initialized;   /* set true only by a successful memoryPoolInit(); see Section 8 */
} MemoryPool_t;
```

`state` is kept as `uint8_t *` rather than `MemoryPoolBlockState_t *` --
one byte per block is the minimum metadata footprint, and the caller's
declared array (`uint8_t state[BLOCK_COUNT];`) stays an obvious, ordinary
byte array rather than requiring the caller to know about an enum type
just to declare storage for it. The enum exists so the implementation
(and this document) always writes and compares `MEMORY_POOL_BLOCK_FREE`
/ `MEMORY_POOL_BLOCK_ALLOCATED` by name, never a bare `0`/`1`. Whether
`memoryPool.h` exposes `state` as part of the public struct at all, or
keeps it opaque, is an API-design-phase decision -- not reopened here.

**Revised (Section 8 reopened) -- an explicit `initialized` flag is kept,
in place of the earlier decision to infer initialization state from
`storage != NULL`.** The original reasoning (a zero-initialized pool
naturally has `storage == NULL`, so no separate flag is needed) is still
correct as far as it goes, but it only covers the zero-initialized case.
A pool declared on the stack without zero-initialization (`MemoryPool_t
pool;`, no `= {0}`) can contain arbitrary garbage in `storage`, which
could coincidentally be non-NULL, silently defeating that check. An
explicit `bool initialized` field, set `true` only inside a successful
`memoryPoolInit()` and checked first by every other operation, decouples
"is this pool ready to use" from "does this specific field happen to be
non-NULL right now" -- see Section 8 for the full reasoning and its own
honest limitation (this doesn't fully close the stack-garbage case
either). Requires `<stdbool.h>`.

`state[]` and `freeStack[]` exist for different reasons and neither
subsumes the other:

- `state[]` answers "is block `i` currently checked out?" in O(1) --
  needed for double-free / invalid-block detection (Section 6) without
  walking anything.
- `freeStack[]` answers "which free block do I hand out next?" in O(1) --
  needed for the free-stack allocation strategy itself (Section 5).

Both are sized in terms of `blockCount`, decided by the caller at
`memoryPoolInit()`, which is why they can't be fixed-size arrays living
inside `MemoryPool_t` itself -- the struct only knows its shape once the
caller tells it how many blocks there are. All three regions (`storage`,
`state`, `freeStack`) are declared and owned by the caller; the pool
never allocates any of them.

**Non-overlap requirement.** The pool does not check, and cannot cheaply
check, whether `storage`, `state`, and `freeStack` refer to
non-overlapping memory -- e.g. a caller accidentally passing the same
array as both `storage` and `state`, or two regions that partially
overlap. Verifying that would mean comparing three arbitrary,
differently-typed regions pairwise at init, which adds real complexity
for a mistake that's avoidable by construction (declare three distinct
arrays). This is a **caller contract**: `storage`, `state`, and
`freeStack` must refer to three genuinely separate, non-overlapping
regions of memory, each with capacity for at least what `memoryPoolInit()`
is told (`blockSize * blockCount` bytes, `blockCount` entries, and
`blockCount` entries, respectively). Passing overlapping regions violates
the API contract -- the pool will read and write through all three as if
they were independent, and overlap will corrupt whichever region got
aliased.

**Resolved trade-off -- `state[]` as one byte per block, not a bitmask.**
A bitmask (`(blockCount + 7) / 8` bytes) would cut this array's RAM cost
8x, at the price of a shift/mask on every `state[]` read or write instead
of a direct byte index. For this design, one byte per block is kept:
simpler code, no bit-twiddling on the acquire/release hot path (Section
10). Unlike the free-stack-vs-scan fork in Section 2, this trade-off has
no O(1)-vs-O(n) guarantee riding on it -- it's a constant-factor RAM
difference either way, which is why simplicity wins here even though a
different consideration won in Section 2. Worth revisiting if a large
pool's metadata footprint becomes a real constraint (e.g. a 1,000-block
pool costs 1 KB of `state[]` alone) -- noted in Limitations rather than
solved speculatively now.

## 5. Allocation Strategy: The Free Stack

Scanning for a free block was rejected as the primary mechanism (Section
2): its cost grows with pool size, which conflicts with the goal of
bounded, history-independent timing. The chosen strategy is a **free
stack** -- an **array-backed LIFO stack of block indices** -- not the more
classic embedded-pointer style where a free block's own memory stores the
next-free-block pointer, and not a bitmap scan either. "Free stack" is
used throughout this document instead of the more generic "free list,"
since a stack (strictly LIFO push/pop) is exactly what's implemented, and
the more specific term avoids any ambiguity with FIFO-ordered or
pointer-linked free-list variants this design does not use.

```
freeStack:  [ 4 | 2 | 0 | 1 | 3 ]
                            ^
                       freeCount = 5 (all free)

Acquire → pop the top (index 3)
freeStack:  [ 4 | 2 | 0 | 1 |   ]      freeCount = 4

Release(block 3) → push back onto the top
freeStack:  [ 4 | 2 | 0 | 1 | 3 ]      freeCount = 5
```

**Why indices in a side array instead of intrusive pointers inside free
blocks:**

- Intrusive lists require `blockSize >= sizeof(void*)`, an arbitrary
  constraint this module has no reason to impose (a 1-byte block should
  be legal).
- Intrusive lists write into "free" block memory the caller doesn't yet
  own -- inconsistent with Section 3's "pool never touches block
  contents" contract, and it would quietly defeat any future caller-side
  poison/canary pattern written into freed blocks.
- It reuses the same indexing scheme as `state[]` -- one consistent
  mental model instead of two.

**Acquire:** if `freeCount == 0`, return `MEMORY_POOL_ERROR_POOL_FULL`,
nothing mutated. Otherwise decrement `freeCount`, read `index =
freeStack[freeCount]`, set `state[index] = MEMORY_POOL_BLOCK_ALLOCATED`,
return `address(index)`.

**Release:** validate the pointer and recover `index` (Section 6). If
invalid, return `MEMORY_POOL_ERROR_INVALID_BLOCK`, nothing mutated. If
`state[index] != MEMORY_POOL_BLOCK_ALLOCATED`, same error, nothing
mutated. Otherwise set `state[index] = MEMORY_POOL_BLOCK_FREE`, push
`freeStack[freeCount] = index`, increment `freeCount`.

Both operations validate fully before mutating anything -- a failed
acquire or release leaves `state[]`, `freeStack[]`, and `freeCount`
bitwise unchanged (see Section 13, Invariants).

Note this makes reuse **LIFO**, not FIFO -- the most-recently-freed block
is the next one handed out. That's a direct consequence of using a stack
for O(1) push/pop, not a chosen fairness policy; flagged in Limitations.

## 6. Pointer Validation and the Double-Free Question

**A note on how this validation is expressed.** `ptr` may be a foreign
pointer -- one that never came from this pool's storage at all, possibly
from a completely unrelated array or object. C only guarantees relational
comparison (`<`, `>=`) and subtraction between pointers that point into
the same array (or one-past its end); doing either between pointers into
two unrelated objects is undefined behavior, not just "probably fine in
practice." Since rejecting exactly that kind of foreign pointer is the
whole point of this validation, the check cannot be built out of direct
pointer comparison/subtraction against `storage` -- doing so would be
relying on undefined behavior to catch the very case it's supposed to
catch. Instead, both `storage` and `ptr` are converted to `uintptr_t`
first, and validation is done as plain unsigned integer arithmetic on
those values, which carries no such restriction:

```c
uintptr_t storageStart = (uintptr_t)pool->storage;
uintptr_t totalSize    = (uintptr_t)(pool->blockSize * pool->blockCount);
uintptr_t storageEnd;   /* computed below, after the overflow guard */
uintptr_t pointer      = (uintptr_t)ptr;
```

To be precise about what this technique actually buys, and not overstate
it: converting a pointer to `uintptr_t` doesn't grant some general-purpose
memory-validation power. All it does is let this specific comparison --
"does this address fall within the range this pool instance already knows
it owns" -- be expressed as ordinary unsigned integer arithmetic instead
of pointer arithmetic between unrelated objects, sidestepping the
undefined behavior described above. The pool can only make this check
because it already knows its own `storage` region's bounds; nothing here
generalizes to validating an arbitrary pointer against arbitrary memory.

**Resolved -- `storageStart + totalSize` is guarded against `uintptr_t`
overflow before `storageEnd` is computed.** For a pool whose `storage`
argument at `memoryPoolInit()` genuinely was `blockSize * blockCount`
bytes (the existing, already-unverified caller contract from Section 4),
this addition cannot actually wrap: a real object's one-past-the-end
address is guaranteed representable by the C standard, and a valid
pointer's `uintptr_t` conversion round-trips exactly, so the only way
`storageStart + totalSize` overflows is if the caller already violated
that storage-size contract -- the same category of already-unverified
caller mistake as the non-overlap requirement (Section 4) and the
alignment contract (Section 7). The guard is added anyway, as an explicit
choice discussed and confirmed rather than skipped:

```c
if (storageStart > (UINTPTR_MAX - totalSize))
{
    return MEMORY_POOL_ERROR_INVALID_BLOCK;
}
storageEnd = storageStart + totalSize;
```

**Honest trade-off, worth stating plainly:** this condition is a static
property of one pool's fixed configuration (`storage`, `blockSize`,
`blockCount` -- all fixed after `Init`), not something that varies by
which `ptr` is being validated. So the guard doesn't behave like the
other four checks in this list, which each reject *one bad pointer* while
leaving the pool otherwise usable -- if it ever triggers, it triggers
identically on *every* future `Release()` call against that same pool,
valid pointers included, since the pool's own notion of its storage
bounds is already unrepresentable. A pool that hits this is, in effect,
permanently unable to accept any release from that point forward. Doing
this check once in `memoryPoolInit()` instead (rejecting the
misconfiguration up front, with a clear Init-time error, rather than
having every subsequent `Release()` silently and uniformly fail) was
considered and not taken -- kept in `Release()` to directly guard the
exact computation that would otherwise be unsafe, matching how the
addition is actually used, at the cost of the confusing "every release
fails identically" failure mode described above if it's ever reached.

Given those, a trustworthy index is recovered by checking, in order:

1. `ptr != NULL`.
2. `pointer >= storageStart && pointer < storageEnd` -- falls within
   *this instance's* storage region. This single check is also what
   rejects a block from a different pool instance being released into
   this one -- a real risk once several pool instances coexist, each
   owned by a different subsystem -- since a block from one pool won't
   fall inside another pool's storage range except by coincidence of
   unrelated memory layout.
3. `(pointer - storageStart) % blockSize == 0` -- lands exactly on a
   block boundary, not partway into one.
4. The resulting `index = (pointer - storageStart) / blockSize` satisfies
   `index < blockCount`.

Any failure returns `MEMORY_POOL_ERROR_INVALID_BLOCK` immediately -- no
further computation, no touching `state[]`. This requires `<stdint.h>`
for `uintptr_t`; the implementation should not fall back to raw pointer
comparison/subtraction against `storage` even as a "simpler" alternative,
since that reintroduces the undefined-behavior problem this section
exists to avoid.

Once `index` is trusted, `state[index]` answers double-free in O(1): if
it's already `MEMORY_POOL_BLOCK_FREE`, release is rejected.

**Resolved fork -- raw pointer over generation-tagged handle (Section
2):** the honest limitation this carries is that the pool cannot
distinguish two situations that both present as
`state[index] == MEMORY_POOL_BLOCK_FREE` at release time -- a true double
free, versus a pointer that happens to satisfy checks 1-4 above and
happens to land on a currently-free block, without ever having actually
come from `acquire()`. Both collapse to `MEMORY_POOL_ERROR_INVALID_BLOCK`.
A generation counter would resolve this, at the cost of the API shape
decided against in Section 2 -- recording that cost precisely here rather
than glossing over it.

## 7. Alignment

No alignment parameter is added. The pool preserves whatever alignment
the caller's storage already has, but does not verify it:

- Because `address(i) = storage + (i * blockSize)`, every block offset is
  a multiple of `blockSize`. If `storage` is aligned to `A` and
  `blockSize` is itself a multiple of `A`, every block is aligned to `A`
  -- preserved by construction, not independently checked.
- If `blockSize` is *not* a multiple of the required alignment, later
  blocks silently drift out of alignment even though block 0 is fine. The
  pool has no way to know at init what type the caller intends to store,
  so this stays a caller obligation, not a checked precondition.

Example: a pool of `struct Foo` needing 8-byte alignment with `sizeof(struct
Foo) == 20` must declare `blockSize` as 24 (rounded up to a multiple of
8), not the raw `sizeof`, or every third block onward loses alignment.

**Carried forward as an API documentation requirement:** this contract is
easy to state but easy to miss if it only lives in a design document
nobody reads before calling `memoryPoolInit()`. `memoryPool.h`'s
documentation for `Init` must say this explicitly, not just imply it --
something to the effect of: *the caller is responsible for ensuring
`storage` is suitably aligned and that `blockSize` preserves the required
alignment for whatever type will be stored in the pool.* Flagging this
now so it isn't dropped once the design phase is behind us and attention
moves to the header.

## 8. Initialization Rules

Rejected outright by `memoryPoolInit()`:

1. `pool`, `storage`, `state`, or `freeStack` is `NULL`.
2. `blockSize == 0`.
3. `blockCount == 0`.
4. `blockSize * blockCount` overflows -- checked before being used for
   anything, and checked **without** performing that multiplication
   directly (computing it and then inspecting the result is already too
   late -- the overflow has happened and the value is wrong by the time
   there's anything to check). The guard divides instead:

   ```
   reject if blockCount > 0 AND blockSize > (SIZE_MAX / blockCount)
   ```

   This is equivalent to asking "would `blockSize * blockCount` exceed
   `SIZE_MAX`" without ever computing the (potentially overflowing)
   product to find out. Checks must run in the order listed: check 3
   (`blockCount == 0`) has to be evaluated, and the init call rejected on
   it, *before* check 4's division ever executes -- dividing by
   `blockCount` when it might be zero is itself unsafe, so this ordering
   isn't optional.
5. `pool->initialized == true` -- see "Re-initialization," directly
   below, for why this is now checked rather than silently allowed.

On success, `state[i] = MEMORY_POOL_BLOCK_FREE` for all `i`,
`freeStack[i] = i` for all `i` (order is functionally irrelevant since
it's a stack -- Section 5's diagram shuffles it only to make push/pop
visually distinct from an identity mapping), `freeCount = blockCount`,
and `initialized = true`.

**Re-initialization of an already-initialized pool (revised).** This was
originally a caller contract (Option C: `Init` twice is unsupported but
not detected, silently resetting the pool and invalidating any
outstanding pointers). That decision is now reversed in favor of Option
B: `memoryPoolInit()` checks `pool->initialized` as check 5 above and
rejects a second call with `MEMORY_POOL_ERROR_ALREADY_INITIALIZED`,
mutating nothing. The reasoning for reversing it: Option C asked the
caller to avoid a mistake "by construction" (initialize each pool exactly
once), but calling `Init` twice on a pool that still has blocks checked
out is a genuinely easy mistake to make by accident (e.g. re-running
setup code on a warm restart path without realizing a pool was already
live), and the failure mode -- silently invalidating whatever pointers
were outstanding -- is bad enough that detecting it outright was judged
worth the small added state, now that `initialized` already exists for
the reason below. Re-initializing a pool that was never used, or that the
caller has no live acquired blocks against, still fails under this rule
too -- the check is purely "has `Init` already succeeded once," not "are
there blocks currently checked out." A caller that legitimately wants to
reset a pool between uses needs a different operation than `Init`; this
module does not currently provide one (not in scope -- see Limitations).

**Uninitialized-pool use** (a requirement: any operation on a pool that
was never successfully initialized must be rejected). This is now
enforced by the `initialized` field (Section 4) rather than inferred from
`storage != NULL`: every operation checks `pool->initialized` first and
returns `MEMORY_POOL_ERROR_NOT_INITIALIZED` if it's not `true`, before
touching `storage`, `state`, or `freeStack` at all. This closes the gap
the earlier `storage != NULL` check left open for a pool declared on the
stack without zero-initialization (`MemoryPool_t pool;`, no `= {0}`),
where `storage` could coincidentally be non-NULL garbage.

**Honest limitation, not fully closed by this change:** the same
stack-garbage scenario can, in principle, also produce a coincidentally
non-zero `initialized` byte -- and for a `bool`, "non-zero" is a much
larger fraction of possible garbage bit patterns than "happens to equal
the specific NULL bit pattern" was for a pointer, so this mechanism is
not strictly stronger against genuinely random uninitialized memory. What
it does reliably fix is the *deliberate* zero-initialized case (`static`,
global, or explicit `= {0}`) staying correctly "not initialized" even
after `storage` is later set to something non-NULL by unrelated code, and
it gives NULL-pointer errors and not-initialized errors distinct,
testable codes instead of conflating them under one. Declare-then-
initialize remains a standing caller responsibility this module cannot
fully enforce without cost disproportionate to the module's scope (e.g. a
wider magic value plus a checksum, which was considered and is still not
adopted).

## 9. Error Codes

Errors are collapsed by the caller's actual next action rather than one
code per individual failure case, to keep the status enum small and
every code actionable:

```c
typedef enum
{
    MEMORY_POOL_SUCCESS = 0,
    MEMORY_POOL_ERROR_NULL_POINTER,
    MEMORY_POOL_ERROR_NOT_INITIALIZED,
    MEMORY_POOL_ERROR_ALREADY_INITIALIZED,
    MEMORY_POOL_ERROR_INVALID_BLOCK_SIZE,
    MEMORY_POOL_ERROR_INVALID_BLOCK_COUNT,
    MEMORY_POOL_ERROR_SIZE_OVERFLOW,
    MEMORY_POOL_ERROR_POOL_FULL,
    MEMORY_POOL_ERROR_INVALID_BLOCK
} MemoryPoolStatus_t;
```

| Status | Covers |
|---|---|
| `MEMORY_POOL_ERROR_NULL_POINTER` | NULL `pool`/`storage`/`state`/`freeStack` at init; NULL output pointer on acquire/a getter |
| `MEMORY_POOL_ERROR_NOT_INITIALIZED` | Any operation other than `Init` itself, called on a pool whose `initialized` field is not `true` (Section 8) |
| `MEMORY_POOL_ERROR_ALREADY_INITIALIZED` | `Init` called on a pool whose `initialized` field is already `true` (Section 8) |
| `MEMORY_POOL_ERROR_INVALID_BLOCK_SIZE` | `blockSize == 0` |
| `MEMORY_POOL_ERROR_INVALID_BLOCK_COUNT` | `blockCount == 0` |
| `MEMORY_POOL_ERROR_SIZE_OVERFLOW` | `blockSize * blockCount` overflow at init |
| `MEMORY_POOL_ERROR_POOL_FULL` | Acquire called with `freeCount == 0` |
| `MEMORY_POOL_ERROR_INVALID_BLOCK` | Release called with a NULL, out-of-range, misaligned, wrong-pool, or already-free pointer (Section 6) -- one code, because the caller's correct response is the same in every case: this release didn't happen, don't trust that the block was returned |

Nine codes total. Splitting `INVALID_BLOCK` into several separate codes
(foreign pointer vs. misaligned vs. already-free) still wouldn't give a
caller anything different to actually do in response, so that collapse
stays as-is. `NOT_INITIALIZED`/`ALREADY_INITIALIZED` are a different
situation: each maps to a genuinely distinct caller action (initialize
the pool first, versus don't initialize it twice), which is why they're
split out now rather than folded into `NULL_POINTER` the way this design
originally had it.

## 10. Complexity

| Operation | Complexity | Why |
|---|---|---|
| `Init` | O(blockCount) | Marks every `state[]` entry `MEMORY_POOL_BLOCK_FREE` and fills `freeStack[]` once. |
| `Acquire` | O(1) | Stack pop, one `state[]` write. No loop. |
| `Release` | O(1) | Fixed number of validation comparisons (Section 6), one `state[]` write, stack push. No loop. |
| `GetFreeCount` / `GetUsedCount` / `GetCapacity` / `GetBlockSize` | O(1) | Direct reads of already-maintained fields. |

The one real arithmetic op on the release path is `(pointer -
storageStart) / blockSize` (Section 6, computed as `uintptr_t` integer
arithmetic, not raw pointer subtraction). Still O(1) (a single instruction
on essentially any target), though if `blockSize` is a power of two it
could compile down to a shift -- a possible future micro-optimization, not
a requirement, noted so it isn't rediscovered as a surprise during
implementation.

## 11. Reentrancy and Concurrency

**Reentrant, but not thread-safe for concurrent access to the same
instance.** Independent pool instances share no state and need no
coordination with each other; concurrent acquire/release into the *same*
instance from independent execution contexts (two threads, or mainline
plus an ISR) requires the caller's own synchronization. No internal
locking is provided. This isn't just "no mutex was added" -- Section 5's
acquire/release sequences are multi-step (read-then-write `freeCount`,
`state[]`, `freeStack[]`), so an interruption mid-sequence can genuinely
corrupt the free stack, not just return a stale statistic. Any pool
instance shared between mainline code and an ISR needs the caller to
guard the call (e.g. disable interrupts around it).

## 12. Memory Ownership

- `storage`, `state`, and `freeStack` are all caller-owned -- the pool
  performs zero dynamic allocation internally, at any point.
- The `MemoryPool_t` descriptor itself is caller-declared; the pool does
  not allocate its own descriptor.
- Individual blocks: acquire *borrows* a block out of caller-owned
  storage; release returns the borrow. The pool's bookkeeping tracks the
  borrow but never owns or interprets block contents (Section 3).

## 13. Invariants

Hold before and after every successful operation, and are never violated
even transiently within the single-caller-per-instance contract of
Section 11:

1. Every block index `i` is exactly one of `MEMORY_POOL_BLOCK_FREE` or
   `MEMORY_POOL_BLOCK_ALLOCATED` at all times.
2. `state[i] == MEMORY_POOL_BLOCK_FREE` iff `i` appears exactly once
   among the first `freeCount` entries of `freeStack[]`.
3. `state[i] == MEMORY_POOL_BLOCK_ALLOCATED` iff `i` appears nowhere in
   those entries.
4. No index appears twice within the first `freeCount` entries at once.
5. `0 <= freeCount <= blockCount`, always equal to the number of
   `MEMORY_POOL_BLOCK_FREE` blocks.
6. A successful acquire only ever returns a block whose state was
   `MEMORY_POOL_BLOCK_FREE` immediately before, and
   `MEMORY_POOL_BLOCK_ALLOCATED` immediately after.
7. A successful release only ever transitions
   `MEMORY_POOL_BLOCK_ALLOCATED` to `MEMORY_POOL_BLOCK_FREE`; an
   unsuccessful release mutates nothing.
8. Any error-returning operation leaves `state[]`, `freeStack[]`, and
   `freeCount` bitwise identical to their state immediately before the
   call.
9. A pointer returned by acquire always lies exactly on a block boundary
   within this instance's storage region.
10. No two simultaneously-`MEMORY_POOL_BLOCK_ALLOCATED` blocks are ever
    handed out as the same address.
11. The pool never reads or writes block contents itself, in either
    acquire or release.
12. `initialized` is `false` before the first successful `Init`, `true`
    from the moment `Init` succeeds through the rest of the pool's
    lifetime, and is never set `false` by any other operation -- there is
    no supported way to "un-initialize" a pool.

These map directly to the planned unit tests (positive/negative paths,
double free, invalid handle, pool full, full alloc/free cycles) -- each
invariant is something a test can assert on directly rather than only
inferring from black-box return values.

## 14. Limitations

- If a pool is ever misconfigured such that `storage + (blockSize *
  blockCount)` would exceed the representable address space (only
  possible if the caller already violated the storage-size contract from
  Section 4), the `uintptr_t` overflow guard in `Release()` (Section 6)
  causes *every* future `Release()` call on that pool to return
  `MEMORY_POOL_ERROR_INVALID_BLOCK` unconditionally, valid blocks
  included, rather than surfacing the misconfiguration once at `Init`
  time. Accepted as a known trade-off (Section 6) rather than moving the
  check to `Init`.
- RAM overhead beyond raw block storage: one `state[]` byte and one
  `freeStack[]` index per block -- the concrete cost of choosing the
  free-stack-plus-detection design over the simpler linear-scan
  alternative (Section 2). `state[]` specifically could shrink 8x as a
  bitmask instead of one byte per block (Section 4); not done here,
  chosen for code simplicity over RAM, revisit if a large pool's metadata
  footprint becomes the binding constraint.
- Cannot distinguish a true double free from a coincidentally-valid,
  never-acquired pointer (Section 6) -- both report as an already-free
  block: `MEMORY_POOL_ERROR_INVALID_BLOCK`. A generation-tagged handle
  would resolve this; not adopted here (Section 2).
- Alignment is caller-preserved, not pool-verified (Section 7).
- No internal synchronization (Section 11).
- Reuse order is LIFO, not FIFO (Section 5) -- not a fairness guarantee,
  a side effect of using a stack for the free-block bookkeeping.
- `initialized` (Section 4, Section 8) narrows but does not eliminate the
  uninitialized-pool gap: it reliably protects a zero-initialized
  (`static`/global/`= {0}`) pool, and now gives NULL-pointer and
  not-initialized errors distinct codes, but a pool declared as
  genuinely random stack garbage can still, in principle, read as
  "initialized" by chance -- arguably no better protected than before for
  that specific case, since a `bool` is "true" for most non-zero garbage
  patterns. Declare-then-initialize remains a standing caller
  responsibility.
- No supported way to reset an already-initialized pool short of a
  process/system restart: `Init` now rejects a pool that's already
  initialized (Section 8) rather than allowing re-initialization, so a
  caller that legitimately wants to reset a pool between uses has no
  in-module operation for that -- out of scope for this revision, not
  overlooked.

## 15. Example Usage

This section exists to make the eventual header concrete enough to review
against -- it isn't new scope, just the Section 4/5/9 API surface shown as
one caller would actually use it:

```c
#define BLOCK_SIZE  32
#define BLOCK_COUNT 8

static uint8_t storage[BLOCK_SIZE * BLOCK_COUNT];
static uint8_t state[BLOCK_COUNT];
static size_t  freeStack[BLOCK_COUNT];

MemoryPool_t pool;

MemoryPoolStatus_t status = memoryPoolInit(&pool, storage, state, freeStack,
                                            BLOCK_SIZE, BLOCK_COUNT);
if (status != MEMORY_POOL_SUCCESS)
{
    /* handle init failure -- bad params, overflow, or already initialized,
     * Section 8 */
}

void *block = NULL;
status = memoryPoolAcquire(&pool, &block);

if (status == MEMORY_POOL_SUCCESS)
{
    /* block points at BLOCK_SIZE usable bytes; use it here */

    status = memoryPoolRelease(&pool, block);
    /* status is MEMORY_POOL_SUCCESS here, since block was validly acquired */
}
else if (status == MEMORY_POOL_ERROR_POOL_FULL)
{
    /* no blocks available right now -- Section 5 */
}
```

Notably absent, on purpose: no `realloc`/resize, no block iteration, no
memory clearing or poisoning, no callbacks, no allocation statistics
beyond the four `Get*` queries, no timeout- or blocking-style acquire, no
built-in synchronization, no automatic alignment handling, no
heap-fallback path, and no in-module way to reset an initialized pool
back to uninitialized (Section 14). `Init` / `Acquire` / `Release` /
`GetFreeCount` / `GetUsedCount` / `GetCapacity` / `GetBlockSize` is the
complete surface this module is scoped to -- each excluded feature would
either dilute a module that's meant to do one thing predictably, or
reopen a fork already settled elsewhere in this document (e.g. blocking
acquire would need the synchronization Section 11 deliberately leaves
out).