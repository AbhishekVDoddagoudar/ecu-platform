# Memory Pool Requirements

Before writing this, I went through general memory-pool material covering
fixed/variable-size pools, allocation algorithms (first-fit, best-fit,
buddy system, segregated lists, slab allocation), fragmentation, and
garbage collection. Most of that describes a *general-purpose dynamic
allocator* — something that itself calls into the OS or heap to obtain its
backing memory, and then subdivides that memory flexibly. That's a
different problem than the one this module can solve, given every other
module in this project explicitly rules out dynamic allocation. So a fair
amount of what follows is as much about explaining what *doesn't* apply
here, and why, as it is about what does.

## What Problem Is This Solving?

Embedded systems frequently ban or heavily restrict `malloc`/`free` for a
few concrete reasons: heap fragmentation accumulates over long uptimes
(months or years without a reboot) until an allocation that always worked
in testing starts failing in the field; allocation timing is
non-deterministic in general (a general allocator may search a free list,
split a block, or coalesce adjacent free blocks, none of which have a
fixed upper bound on how long they take); and allocation failures are
possible at any point in a program's life, not just predictably at
startup.

A fixed-size memory pool solves the specific slice of this problem that
most embedded code actually needs: **an application that repeatedly needs
same-sized chunks of memory** (message buffers, protocol frames, task
control blocks, event structs) can pre-reserve exactly enough storage for
N of them, once, at a point the caller controls, and then obtain and
release individual blocks from that fixed reservation in guaranteed
bounded time, with no possibility of external fragmentation, because every
block is interchangeable by construction.

## Responsibilities

- Hand out one fixed-size block of memory per request, from a pool the
  caller has already reserved.
- Accept a previously-handed-out block back, making it available for
  reuse.
- Track which blocks are currently checked out vs. free.
- Report exhaustion explicitly when no free block remains, rather than
  returning something that looks valid but isn't.
- Detect misuse where it's actually feasible to detect it (e.g. releasing
  something twice) rather than silently corrupting its own bookkeeping.

That's the whole job: **hand out, take back, track, and fail loudly when
something's wrong.**

## Explicitly Out of Scope

Going back through the material I read, here's what applies to a
general-purpose allocator but not to this module, and why:

- **Variable-size allocation.** Every block in a given pool instance is
  the same fixed size. An application needing several different object
  sizes needs several separate pool instances — this module isn't trying
  to be a general allocator that happens to be embedded-friendly.
- **Fragmentation-reduction algorithms** (first-fit, best-fit, worst-fit,
  buddy system, segregated lists, slab allocation). These exist
  specifically to solve the problem of finding a large-enough contiguous
  region among variably-sized free chunks. A fixed-block pool doesn't have
  that problem *by construction* — a free block is always exactly the
  right size for the next request, because there's only one size. None of
  that machinery is relevant here.
- **Coalescing.** Merging adjacent free blocks only matters when blocks
  can vary in size and you want to reconstitute a larger one. Fixed-size
  blocks are never split or merged.
- **Garbage collection.** Blocks are explicitly acquired and explicitly
  released by the caller, the same contract as `malloc`/`free` — nothing
  is automatically reclaimed because nothing tracks reachability.
- **Calling into the OS or heap at all**, for its own backing storage. A
  general memory pool (per the material) typically *does* request one
  large region from the system up front and then subdivides it. This
  module's backing storage is caller-provided static or stack memory —
  see Memory Ownership below for why that's a meaningfully different
  relationship than "the pool owns memory it got from the heap."
- **Internal thread-safety/locking.** Same contract as every other module
  in this project: reentrant in the sense of "safe for independent
  callers using independent pool instances," not internally
  lock-protected against concurrent access to the *same* pool instance.
- **Interpreting block contents.** The pool hands back raw memory (or a
  typed pointer); what the caller stores there is entirely the caller's
  business, same as `malloc`.

## Inputs

- **To initialize a pool**: caller-provided backing storage, the size of
  each block, and the number of blocks. This is structurally very close to
  `ringBufferInit(storage, capacity, elementSize)` — a caller-owned array
  plus dimensions.
- **To acquire a block**: essentially nothing beyond the pool reference
  itself. Unlike `ringBufferPush`, there's no data being handed *in* —
  acquiring a block is closer to `malloc`'s shape (give me space; I'll
  write into it myself) than to a copy-in operation. Since block size is
  fixed per pool, there isn't even a size parameter to pass at
  acquire-time — the pool already knows it.
- **To release a block**: the pool reference, and whatever identifies the
  specific block being returned (a raw pointer, or a handle — not
  deciding which yet).

## Outputs

- **From initialization**: a status/result code.
- **From acquiring a block**: a pointer (or handle) to the block, plus a
  status/result code distinguishing "here's your block" from "pool is
  exhausted, there's nothing to give you."
- **From releasing a block**: a status/result code.
- **Possibly, as diagnostics**: how many blocks are currently free, how
  many are in use, and the pool's total capacity — mirroring
  `ringBufferGetSize`/`GetCapacity`/`GetAvailableSpace`. Not committing to
  all three existing as separate calls, just noting the same category of
  question exists here too.

## Failure Cases

Trying to think through this from scratch rather than by analogy to a
specific existing allocator:

1. NULL pool pointer.
2. NULL backing storage pointer at initialization.
3. Block size of 0 (a zero-byte block can't hold anything — nonsensical).
4. Block count of 0, or some other degenerate capacity. Whether this
   should be a distinct rejected error or just an always-exhausted pool is
   genuinely the same open question that came up for the ring buffer and
   the timer pool — not resolving it here, just naming it.
5. Acquiring a block when the pool is exhausted (no free blocks left).
6. Releasing a pointer that was never obtained from this pool at all — a
   foreign or garbage pointer. Whether this is even *detectable* depends
   entirely on what bookkeeping the implementation chooses to keep (does
   it validate the pointer falls within the backing storage's address
   range and lands exactly on a block boundary?) — flagging this as a real
   and possibly expensive validation question, not assuming it's free.
7. Releasing a pointer that *is* within the pool's storage range, but
   misaligned — pointing partway into a block instead of at its start. A
   subtler variant of #6.
8. Double-release: releasing a block that's already free (i.e. releasing
   the same pointer twice without re-acquiring it in between).
9. Any operation attempted on a pool that was never successfully
   initialized.
10. A NULL output-pointer argument on whichever call is responsible for
    handing back the acquired block's address, depending on how that API
    ends up shaped.
11. **Use-after-free**: code continues writing to a block after releasing
    it. I don't think this is detectable by the pool itself without extra
    cost (a poison/canary pattern, or similar) — worth stating plainly as
    a caller responsibility and a fundamental limitation of this category
    of tool in C, not something a later design pass can simply fix for
    free.
12. Arithmetic overflow if anything ever needs to compute `blockSize *
    blockCount` (e.g. to sanity-check that the caller's storage array is
    actually big enough) — the same class of concern packetParser's
    requirements analysis flagged for its own total-size computation.

## Memory Ownership

The backing storage is caller-owned — declared by the caller (a static or
stack array, sized to `blockSize * blockCount` or equivalent) and handed
to initialization, the same relationship `ringBuffer` has with its
storage. The pool never allocates this itself.

Here's the distinction worth being explicit about, tying back to what I
read: a *general* memory pool, per that material, typically **does**
request one large region from the OS/heap up front and then subdivides it
— the pool itself is something that owns heap-obtained memory. **This
module never does that.** It never asks anything for memory at all. What
it actually is, underneath, is a bookkeeping layer over memory the caller
already unconditionally owns — tracking which fixed-size regions of that
already-owned memory are "checked out" right now versus free. "Allocation"
here is purely logical (flipping a used/free marker), never physical
(never asking an OS, a heap, or anything else for bytes). That's a
meaningfully different relationship than the general pattern described in
the material, and I think it's worth stating outright rather than letting
the word "pool" imply the heap-backed version by default.

Individual acquired blocks are, in the same sense, *borrowed* from that
caller-owned storage for as long as the caller holds onto them —
conceptually close to how packetParser's zero-copy payload is borrowed
from the caller's raw buffer, except here the "borrowing" is the module's
entire purpose rather than an incidental design choice.

## Guarantees

- **No external fragmentation is possible, structurally** — not "reduced,"
  not "mitigated by an algorithm," actually impossible, because every
  block is the same size. A block freed at any point is immediately usable
  for literally any future request, regardless of allocation history. This
  is the core property the whole design exists to provide, so it's worth
  stating as a guarantee rather than an implementation detail.
- **Acquire and release should take bounded, predictable time** that
  doesn't grow with how many blocks are currently in use or how long the
  pool has been running. I'm not committing to O(1) specifically as a
  requirement — that's a design/implementation decision — but "bounded and
  independent of history" is a genuine requirement, not just a nice-to-have,
  given this module exists specifically for environments where allocation
  timing needs to be predictable.
- **Total capacity is known and fixed** the moment initialization succeeds
  — never more than `blockCount` blocks can be in use at once, and that
  number doesn't change at runtime.
- **No dynamic allocation is ever performed internally**, at any point,
  under any call.

## Limitations Callers Should Understand

- **Fixed block size per pool instance.** An application needing several
  distinct object sizes needs several distinct pools, not one pool that
  adapts — a real, honest cost of this approach versus a general
  allocator, not something to gloss over.
- **Internal fragmentation is still possible and is the caller's cost to
  manage** — if a caller picks a block size larger than what most
  individual allocations actually need, that excess is wasted for every
  block in the pool, for the pool's entire lifetime. Unlike external
  fragmentation, this isn't something the module can prevent; it's a
  direct consequence of the block-size the caller chose at init time.
- **Fixed total capacity, decided at initialization** — no growth at
  runtime, consistent with no dynamic allocation.
- **Use-after-free is not detectable** by this module in general, per
  Failure Case #11 — this is a caller-discipline requirement, not a gap
  that a more careful implementation could close for free.
- **Whether foreign-pointer and double-release misuse are actually
  detected** depends on implementation choices not made yet (Failure Cases
  #6–#8) — not promising detection here that the design phase hasn't
  earned yet.
- **No internal synchronization** — concurrent acquire/release calls into
  the *same* pool instance from independent execution contexts require the
  caller's own synchronization, consistent with every other module's
  reentrancy contract in this project.