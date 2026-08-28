# CRC-32 Design

This document resolves the open design questions left by
`crc32-requirements.md` with concrete decisions and the reasoning behind
each one. `crc32.h` is not written yet — this is the "how," ahead of the
API declarations, following the same process `memoryPool-design.md` used.

## 1. Problem Statement

Compute CRC-32/ISO-HDLC (poly `0x04C11DB7`, init `0xFFFFFFFF`, RefIn/RefOut
enabled, XorOut `0xFFFFFFFF`) over caller-supplied data, in both one-shot
and incremental form, matching the canonical check value `0xCBF43926` for
`"123456789"`. This document decides the internal bit-processing algorithm,
the context's shape, how reflection and the final XOR are actually applied
in code (not just stated as parameters), and the handful of correctness
edge cases that follow from that.

## 2. Internal Algorithm: The Reflected (Right-Shift) Form

**The question the requirements doc deferred:** implement reflection by
literally reversing bits — reverse each input byte, run the same
left-shift, MSB-first structure `crc16` already uses with the normal
polynomial `0x04C11DB7`, then reverse the final 32-bit result — or use the
standard reflected algorithm, which processes bytes as-is and shifts
*right* using the bit-reversed polynomial `0xEDB88320`?

**Fork A — explicit bit-reversal + crc16-style left-shift core.**
Reflect each input byte (a small lookup table or a reverse-bits loop),
feed it through a left-shift, MSB-first core identical in shape to
`crc16ProcessByte`, using the polynomial exactly as stated in the
requirements (`0x04C11DB7`), then reflect the final accumulator once at
the end. This has the appeal of literally matching the requirements
table's polynomial value in the code, and of visibly reusing `crc16`'s
existing bit-shifting shape — but it needs a working bit-reversal routine
for every byte processed (either a per-call loop or a 256-entry reversal
table), on top of the CRC computation itself, and one more reversal on the
final result.

**Fork B (chosen) — right-shift algorithm with the reflected polynomial
`0xEDB88320`.** Process each input byte's bits directly, unreflected, but
shift the accumulator *right* instead of left, testing and reacting to the
LSB instead of the MSB, and XOR with `0xEDB88320` — the bit-reversal of
`0x04C11DB7` — instead of `0x04C11DB7` itself. This is mathematically
equivalent to Fork A's "reflect input, run normal algorithm, reflect
output" for a very concrete reason: reflecting a bit sequence and then
running the algorithm MSB-first, versus running the algorithm LSB-first on
the un-reflected sequence, walk the same bits in the same relative order —
shifting the frame of reference (which end you start from) instead of
physically reversing the data. Feeding bits in from the LSB side with a
right-shifting core is the same computation as feeding reflected bits in
from the MSB side with a left-shifting core, just without ever materially
reversing anything. `0xEDB88320` is not a different or "alternate" CRC-32
polynomial — it is the bit-reversal of `0x04C11DB7`, arising *because* the
processing direction flipped, not because a different generator polynomial
was chosen. Concretely:

```
0x04C11DB7 = 0000 0100 1100 0001 0001 1101 1011 0111
             (reversed bit-for-bit)
0xEDB88320 = 1110 1101 1011 1000 1000 0011 0010 0000
```

**Why B was chosen:** it is the standard, universally-used
implementation technique for reflected CRCs — zlib, PNG, Ethernet drivers,
and virtually every real-world CRC-32 implementation use exactly this
right-shift-with-reversed-polynomial form, for good reason: RefIn and
RefOut, which Fork A would implement as two explicit reversal passes, come
out *for free* as a consequence of which direction the shift already goes
— no separate reflection step exists to get wrong. Fewer moving parts,
better precedent to be judged against in a review, and it is the form a
later table-driven optimization (noted as future scope in the
requirements doc) would extend directly, since precomputed CRC tables are
built from this exact reflected-polynomial form. `0x04C11DB7` stays the
value stated in `crc32-requirements.md` and in this module's documentation
header, since that's the polynomial's conventional name and what a reader
would search for — `0xEDB88320` appears only inside the implementation,
documented as its bit-reversal, so nobody reviewing the code mistakes it
for a second, different variant.

## 3. Context Representation and the Uninitialized-Context Question

```c
typedef struct
{
    uint32_t currentCRC;
} CRC32_Context_t;
```

One member, matching `CRC16_Context_t`'s shape exactly. The requirements
doc explicitly left open whether an `initialized` flag belongs here, the
way `MemoryPool_t` carries one. **Decision: no flag is added.** The
memory pool's `initialized` field exists to prevent operations on
`storage`/`state`/`freeStack` that would otherwise touch memory outside
what `Init` set up — real memory-safety consequences, on caller-owned
buffers whose size the pool itself chose. Nothing analogous exists here:
`currentCRC` is a single `uint32_t` value inside the context struct
itself, not a pointer into external caller-owned memory, and no CRC32
operation reads or writes anything the context doesn't already, entirely,
contain. Calling `crc32Update`/`GetResult` before `crc32Init` doesn't
corrupt anything or touch memory it shouldn't — it produces a CRC computed
from whatever `currentCRC` happened to already hold, which is simply the
*wrong number*. That's a correctness bug, not a memory-safety one, and
it's a bug the check-value test (Section 8) and any real integration
(comparing against an independently-computed CRC on the other end of a
link) surfaces immediately and unambiguously — there's no silent-corruption
failure mode to guard against the way there was for the pool. This also
keeps `CRC32_Context_t` a direct sibling of `CRC16_Context_t`, which
carries no such flag either, for the same reasons.

`crc32Init()` itself cannot fail for any reason related to its own
parameters — unlike `memoryPoolInit()`, there is no `blockSize`,
`blockCount`, or overflow to validate, only the fixed initial value from
the locked parameters. Its only possible failure is `ctx == NULL`.

## 4. Update Algorithm

Byte-at-a-time, bit-by-bit, per the requirements doc's decision to
implement a reflected, bit-by-bit, right-shift algorithm first and defer a table-driven
version:

```c
CRC32_Status_t crc32Update(CRC32_Context_t *ctx, const uint8_t *data, size_t len)
{
    if (ctx == NULL) return CRC32_ERROR_NULL_POINTER;
    if (data == NULL && len > 0) return CRC32_ERROR_NULL_POINTER;

    uint32_t crc = ctx->currentCRC;

    for (size_t i = 0; i < len; i++)
    {
        crc ^= (uint32_t)data[i];

        for (int bit = 0; bit < 8; bit++)
        {
            if (crc & 0x00000001U)
            {
                crc = (crc >> 1) ^ CRC32_REFLECTED_POLYNOMIAL; /* 0xEDB88320 */
            }
            else
            {
                crc = crc >> 1;
            }
        }
    }

    ctx->currentCRC = crc;
    return CRC32_SUCCESS;
}
```

`len == 0` falls straight through the outer loop unchanged — a no-op by
construction, not a special case, same as `crc16Update`. The `data ==
NULL && len == 0` allowance is checked exactly like `crc16`'s
`validateBuffer` logic: NULL is only rejected when there's actually data
it's supposed to point to.

**Note this stores the raw right-shifted accumulator, not the final
XORed value, in `ctx->currentCRC` between calls.** That's deliberate —
see Section 6.

## 5. Reflection Handling — Why There's No Separate Step

Per Section 2's chosen algorithm, RefIn and RefOut are not separate
operations anywhere in this module's code — they are structural
consequences of processing bits LSB-first with a right-shift and the
reflected polynomial. There is no function, flag, or code path labeled
"reflect input" or "reflect output"; searching for one in the
implementation is expected to come up empty. This is the payoff of
choosing Fork B in Section 2 and is worth stating explicitly so a reviewer
doesn't go looking for reflection logic that was never meant to exist as
a distinct step.

## 6. Final XOR — Applied Once, at Read Time, Never Stored

The final XOR (`0xFFFFFFFF`) is applied only inside `crc32GetResult`, on a
*copy* of `ctx->currentCRC` — never written back into the context, and
never applied inside `crc32Update`:

```c
CRC32_Status_t crc32GetResult(const CRC32_Context_t *ctx, uint32_t *crc)
{
    if (ctx == NULL || crc == NULL) return CRC32_ERROR_NULL_POINTER;

    *crc = ctx->currentCRC ^ CRC32_FINAL_XOR; /* 0xFFFFFFFF */
    return CRC32_SUCCESS;
}
```

**Why this matters, not just a style choice:** the final XOR is defined
by the spec as something applied exactly once, to the finished
calculation — not once per byte and not once per `Update` call. If it were
applied inside `Update` instead, calling `Update` twice (e.g. `Update(buf1,
len1)` then `Update(buf2, len2)`, the documented incremental-use pattern)
would XOR the running value twice, which does not cancel out and does not
equal a single XOR at the end — it silently produces the wrong CRC for
any caller doing incremental updates, while still happening to look
correct for a caller who only ever calls `Update` once. That's exactly
the kind of bug that survives a naive test (single-buffer input) and
fails only on the streaming path the requirements doc specifically calls
out as a use case. `GetResult` is also documented (matching `crc16`) to
not invalidate the context — further `Update` calls after `GetResult` are
allowed and must continue from the correct pre-XOR accumulator, which is
only guaranteed if the XOR was never written back into `ctx->currentCRC`
in the first place.

`crc32Compute` (one-shot) is a thin wrapper — `Init`, `Update`,
`GetResult`, in that order, no logic duplicated — identical relationship
to `crc16Compute`.

## 7. Result Representation

`crc32GetResult`/`crc32Compute` produce a plain `uint32_t` in host integer
representation — not a byte array, not a specified wire byte order. This
module has no notion of how the 4-byte CRC gets serialized into a frame
(big-endian, little-endian, or otherwise) — that's `packetBuilder`'s
concern when it places a CRC-32 field into a frame, same relationship
`crc16` already has with `packetBuilder`/`packetParser` for the 16-bit
case. Stated explicitly here so it isn't silently assumed one way during
implementation and a different way during integration.

## 8. Verify Semantics

```c
CRC32_Status_t crc32Verify(const uint8_t *data, size_t len, uint32_t receivedCRC)
{
    uint32_t calculated;
    CRC32_Status_t status = crc32Compute(data, len, &calculated);

    if (status != CRC32_SUCCESS)
    {
        return status; /* parameter failure, not a mismatch */
    }

    return (calculated == receivedCRC) ? CRC32_SUCCESS : CRC32_ERROR_MISMATCH;
}
```

Same three-way outcome `crc16Verify` gives: match (`CRC32_SUCCESS`),
mismatch (`CRC32_ERROR_MISMATCH`, a legitimate outcome, not a
malfunction), or a parameter failure that means the comparison was never
actually attempted (`CRC32_ERROR_NULL_POINTER`) — a caller must not treat
a parameter failure as "mismatch," since no calculation happened to
mismatch in the first place.

## 9. Status Codes

```c
typedef enum
{
    CRC32_SUCCESS = 0,
    CRC32_ERROR_NULL_POINTER,
    CRC32_ERROR_MISMATCH
} CRC32_Status_t;
```

Deliberately identical in shape to `CRC16_Status_t` — three codes, same
meanings, same names with the width swapped in. No `CRC32`-specific error
exists because, per Section 3, there's no initialization state to be
"not initialized," and per Section 4, `len == 0` is a defined no-op, not
an error condition needing its own code.

## 10. Integer Types and Portability

- `uint32_t` (`<stdint.h>`) throughout — the CRC accumulator, the result,
  and the polynomial/init/xorout constants are all explicitly 32-bit, no
  reliance on `int`/`unsigned int` happening to be 32 bits on a given
  target.
- `size_t` for `len`, matching `crc16`.
- No signed integer types touch the calculation at any point, avoiding
  any implementation-defined right-shift-of-negative-value behavior —
  `crc >> 1` in Section 4 operates on `uint32_t`, where right-shift is
  well-defined (logical, zero-filled) per the C standard, not
  implementation-defined the way it would be for a signed type.
- The inner bit loop (`for (int bit = 0; bit < 8; bit++)`) uses `int` for
  the loop counter only — never for anything CRC-value-related — which is
  fine and conventional since it never approaches any width limit.

## 11. API Surface

```c
CRC32_Status_t crc32Init(CRC32_Context_t *ctx);
CRC32_Status_t crc32Update(CRC32_Context_t *ctx, const uint8_t *data, size_t len);
CRC32_Status_t crc32GetResult(const CRC32_Context_t *ctx, uint32_t *crc);
CRC32_Status_t crc32Compute(const uint8_t *data, size_t len, uint32_t *crc);
CRC32_Status_t crc32Verify(const uint8_t *data, size_t len, uint32_t receivedCRC);
```

Five functions, matching `crc16.h`'s surface exactly with the width
swapped — no polynomial/init/refin/refout/xorout parameters anywhere in
the signatures, per the requirements doc's explicit decision not to make
this module runtime-configurable.

## 12. Test Strategy

- **Canonical check value:** `crc32Compute("123456789", 9, &crc)` must
  yield `0xCBF43926`. This is the single most important test in the
  suite — it's the one that would catch a reflection or final-XOR bug
  that every other test might miss (Section 6).
- **Empty input:** `len == 0` on `Compute`/`Verify` must yield the
  final-XORed initial value (`0xFFFFFFFF ^ 0xFFFFFFFF = 0x00000000`),
  confirming the no-op path (Section 4) composes correctly with the final
  XOR (Section 6) rather than being tested in isolation from it.
- **Incremental equals one-shot:** for the same data split at several
  different byte boundaries (including a 1-byte-then-rest split and a
  multiple byte-boundary splits — 1 byte + remainder, a split half-way
  through the buffer, and several small chunks), the result must equal
  `crc32Compute` on the whole buffer at once. This is the test that
  would have caught the XOR-applied-inside-Update bug described in
  Section 6, so it's treated as load-bearing, not incidental.
- **`GetResult` non-destructive:** call `GetResult`, then call `Update`
  again with more data, then `GetResult` again — the second result must
  equal what a single `Compute` over the full concatenated data would
  produce, confirming the final XOR truly never gets written back into
  `ctx->currentCRC`.
- **Single-bit and single-byte inputs**, and a few arbitrary multi-byte
  buffers with independently-known-correct CRC-32 values (cross-checked
  against a second, trusted implementation or online calculator during
  test authoring, not hand-derived).
- **NULL handling:** NULL `ctx` on every context-based call; NULL `data`
  with `len > 0` (must error) and NULL `data` with `len == 0` (must
  succeed, matching `crc16`'s contract); NULL output pointer on
  `GetResult`/`Compute`.
- **Verify:** a known-good buffer/CRC pair (`CRC32_SUCCESS`), the same
  buffer with the CRC value deliberately corrupted
  (`CRC32_ERROR_MISMATCH`), and a NULL-data/`len > 0` call
  (`CRC32_ERROR_NULL_POINTER`, confirming a parameter failure is never
  reported as a mismatch — Section 8).

## 13. Limitations

- Implements exactly one CRC-32 variant (ISO-HDLC); no runtime
  configuration, per the requirements doc.
- Direct bit-by-bit processing, not table-driven — correct, but slower
  per byte than a lookup-table implementation. Table-driven is explicitly
  future scope (requirements doc), and per Section 2, would be built
  directly from the same reflected-polynomial form already chosen here,
  not a redesign.
- No `initialized` tracking in the context (Section 3) — calling
  `Update`/`GetResult` before `Init` is a caller error that produces a
  wrong numeric result rather than a detected, reported error. Considered
  acceptable given this module touches no memory beyond its own context
  struct, unlike the memory pool, where the equivalent gap was judged
  worth closing.
- No internal synchronization — concurrent use of a single shared context
  from multiple execution contexts is the caller's responsibility, same
  as `crc16` and every other module in this project.
- This module produces a raw `uint32_t` result only; wire-format
  byte-serialization is explicitly out of scope (Section 7).