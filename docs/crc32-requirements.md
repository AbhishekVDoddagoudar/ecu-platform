# CRC-32 Requirements

## Relationship to CRC16 — Standalone, Not a Generalization

Before anything else: this module is a **standalone** module, structurally
independent of `crc16`, not a generalized "CRC engine" that both CRC16 and
CRC32 plug into.

The reason is concrete, not just a preference. `crc16`'s bit-processing
core (`crc16ProcessByte`) is hardwired for the one variant it supports —
CRC-16-CCITT-FALSE — which needs no input reflection, no output
reflection, and no final XOR. None of that machinery exists anywhere in
`crc16`, by design. Standard CRC-32 (CRC-32/ISO-HDLC — Ethernet, ZIP, PNG,
gzip) needs *all three*: input reflection, output reflection, and a final
XOR. Retrofitting that into `crc16`'s existing shape would mean either
bolting reflection logic onto a function that was deliberately built
without it, or building an actual parameterized CRC engine (width, poly,
init, refin, refout, xorout all as parameters) — a materially different
and larger module than either `crc16` or `crc32` need to be individually.

This project's own stated principle is "simplicity over cleverness."
A generic CRC engine is the more "textbook" answer, but with exactly two
concrete variants in front of it — one of which doesn't even need the
generic machinery — building one now would be generalizing from a sample
size of one. If a third CRC variant shows up later needing yet another
combination of these parameters, *that's* the point where extracting a
shared engine actually pays for itself. Not before.

So: `crc32` gets its own header, its own `.c` file, its own context
struct, its own status enum, and its own init/update/getResult/compute/
verify API surface — mirroring `crc16`'s file layout and naming
conventions closely enough that the two modules read as siblings, but
sharing no code.

## Locked Parameters

This is the CRC-32/ISO-HDLC variant (also known plainly as "CRC-32," and
as "PKZIP" / "Ethernet CRC-32" in other contexts). Pinning every parameter
explicitly here, before implementation, exists to prevent the classic
failure mode where two things both get called "CRC-32" but produce
different results because reflection or the final XOR were assumed rather
than stated.

| Parameter        | Value                                   |
|-------------------|------------------------------------------|
| Polynomial        | `0x04C11DB7` (normal form)               |
| Initial value     | `0xFFFFFFFF`                             |
| Input reflection  | Enabled                                  |
| Output reflection | Enabled                                  |
| Final XOR         | `0xFFFFFFFF`                             |
| Width             | 32 bits                                  |
| Check value       | `0xCBF43926` for ASCII input `"123456789"` |

The check value is the standard reference test vector for this variant.
Any implementation that doesn't reproduce `0xCBF43926` for that exact
9-byte ASCII input has a parameter or reflection bug somewhere, regardless
of how plausible its output looks on other input.

**Explicitly not chosen here:** CRC-32/MPEG-2 (same polynomial, but no
reflection and no final XOR — used in some AUTOSAR/BSW E2E contexts).
If a MPEG-2-style variant is ever needed, it is a *different* set of
locked parameters and arguably a separate module, not a flag on this one
— consistent with the standalone-module decision above.

## What Problem Is This Solving?

Same fundamental job as `crc16`: detect accidental corruption in a block
of data — bit flips from a noisy channel, a flaky flash sector, a
truncated transfer — by reducing the data to a fixed-size checksum that a
receiver can recompute and compare. CRC-32 exists alongside CRC16 in this
project because different contexts call for different tradeoffs between
checksum strength and overhead. CRC-32 provides a 32-bit error-detection
code and, for this polynomial and the message lengths this project deals
with, stronger error-detection characteristics than the project's 16-bit
CRC, at the cost of two additional bytes per frame/record — not simply
"more bits, therefore better" in general, since error-detection capability
is a function of polynomial, message length, and error pattern, not bit
width alone. A platform aiming to resemble real automotive software needs
to demonstrate handling both tradeoffs appropriately rather than picking
one CRC width and using it everywhere by default.

## Responsibilities

- Compute a CRC-32/ISO-HDLC checksum over a buffer, in one shot.
- Support incremental/streaming computation across multiple calls, for
  data that arrives in pieces (matching `crc16`'s Init/Update/GetResult
  shape).
- Verify a received CRC-32 value against data, distinguishing "matches"
  from "does not match" from "couldn't even attempt the comparison due to
  a parameter error."
- Correctly apply input reflection, output reflection, and the final XOR
  — the three things that make this "CRC-32" and not just "some 32-bit
  checksum."

## Explicitly Out of Scope

- **Any variant other than CRC-32/ISO-HDLC.** No runtime-configurable
  polynomial, no support for MPEG-2 or other 32-bit variants. If that's
  ever needed, it's new scope, not a hidden option in this module.
- **Table-driven optimization, as an initial requirement.** A lookup-table
  implementation is the standard real-world optimization for CRC-32 and
  may be worth adding later to demonstrate that technique specifically,
  but the first implementation should be the direct bit-by-bit reflected
  algorithm — easier to verify against the spec line-by-line, and it's
  what the correctness of any later table-driven version would be
  validated against.
- **Being the generalization point for CRC16.** Covered above.
- **Interpreting what the CRC is protecting.** Same as `crc16` — this
  module has no notion of packets, frames, or fields. It checksums bytes
  it's handed. Any packet-level integration is `packetParser`/
  `packetBuilder`'s concern, not this module's.

## Inputs

- **To initialize a context**: nothing but the context pointer itself —
  the initial value is fixed by the locked parameters above, not supplied
  by the caller. Same shape as `crc16Init`.
- **To update**: the context, a data buffer pointer, and a length —
  identical shape to `crc16Update`, including that `len == 0` is a
  well-defined no-op rather than an error (consistent with `crc16`'s
  documented behavior).
- **To get the result**: the context, and an output pointer for the
  32-bit CRC value.
- **To compute (one-shot)**: a data buffer and length, no context needed
  from the caller — same convenience-wrapper relationship `crc16Compute`
  has to `crc16Init`/`Update`/`GetResult`.
- **To verify**: a data buffer, length, and the received CRC value to
  check against.

## Outputs

- **From initialization**: a status/result code.
- **From update**: a status/result code.
- **From get-result**: a status/result code, and (via output pointer) the
  current 32-bit CRC value — reflection and final XOR already applied, so
  the caller always receives the final, spec-correct value rather than a
  raw accumulator they'd need to post-process themselves.
- **From compute**: same as get-result, in one call.
- **From verify**: a status/result code distinguishing match, mismatch,
  and parameter-error outcomes — same three-way distinction `crc16Verify`
  makes.

## Failure Cases

Working through this the same way the CRC16 and Memory Pool requirements
did — from scratch, not just by analogy:

1. NULL context pointer on any context-based call.
2. NULL data pointer while `len > 0` (mirrors `crc16`'s `validateBuffer`
   logic exactly — `data == NULL` with `len == 0` is a valid no-op, not an
   error).
3. NULL output pointer on get-result or compute.
4. Operating on a context that was never initialized. Whether this is
   actually detectable depends on whether the context carries any
   "initialized" marker beyond the raw accumulator — an open
   implementation question, flagged here rather than assumed away.
5. Getting a mismatched result specifically due to a reflection bug (e.g.
   RefIn applied but RefOut forgotten, or vice versa) — not a runtime
   failure case in the API sense, but the single most likely *correctness*
   bug in this module, which is precisely why the check value
   (`0xCBF43926` for `"123456789"`) is being pinned now rather than
   discovered missing during a code review later. The unit tests must
   include this exact vector, not just arbitrary self-consistency checks.
6. Verify: the calculated CRC not matching the caller-supplied
   `receivedCRC` — a legitimate outcome (`CRC32_ERROR_MISMATCH`), not a
   malfunction, same as `crc16Verify`.

## Guarantees

- Bit-for-bit conformance with the standard CRC-32/ISO-HDLC definition,
  validated against the canonical check value.
- No dynamic memory allocation, at any point, under any call — consistent
  with every other module in this project.
- Reentrant: all calculation state lives in the caller-owned context
  struct, so independent callers using independent contexts may compute
  CRCs concurrently. No internal synchronization is provided for
  concurrent access to the *same* context — same contract as `crc16` and
  every other module here.
- `len == 0` is a well-defined no-op at every call site that accepts a
  length, never an error.

## Limitations Callers Should Understand

- This module implements exactly one CRC-32 variant. Code that needs a
  different 32-bit CRC (MPEG-2, BZIP2, etc.) needs a different module —
  this one will silently produce ISO-HDLC results regardless of what the
  caller's protocol actually expects, and there is no parameter to
  override that.
- Like `crc16`, this module detects accidental corruption. It provides no
  protection against deliberate tampering — CRCs are not a cryptographic
  primitive, and nothing here should be read as implying otherwise.
- No internal synchronization — concurrent use of a single shared context
  from multiple execution contexts is the caller's responsibility to
  serialize.