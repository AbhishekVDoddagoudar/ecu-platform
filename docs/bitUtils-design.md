# bitUtils Design

## 1. Problem Statement

The BitUtils module provides reusable bit manipulation utilities for the ECU Platform. It offers a consistent API for manipulating individual bits and bitfields while avoiding platform-specific dependencies. The module is intended to be reused by higher-level components such as CRC, Packet Parser, CAN drivers, and configuration management.

## 2. Design Goals

- Reusable: functions can be used across multiple modules and components.
- Lightweight: minimal code footprint and simple implementation.
- No dynamic allocation: operations are performed on existing values without heap usage.
- Portable: written in a way that works across compilers and MCU targets.
- Independent: does not depend on platform-specific drivers or OS features.
- Deterministic: All operations execute in constant time (O(1)).
- Reentrant: The module maintains no internal state and is safe for concurrent use by multiple callers.
- Fail-safe: Invalid input is rejected with an explicit status code rather than triggering undefined behavior.

## 3. Supported Operations

- `bitUtilsSetBit(value, bitPosition, updatedValue)` - Sets a single bit (0-31) in `value` to 1, writing the result to `*updatedValue`.
- `bitUtilsClearBit(value, bitPosition, updatedValue)` - Clears a single bit (0-31) in `value` to 0, writing the result to `*updatedValue`.
- `bitUtilsToggleBit(value, bitPosition, updatedValue)` - Flips a single bit (0-31) in `value`, writing the result to `*updatedValue`.
- `bitUtilsIsBitSet(value, bitPosition, isSet)` - Reads a single bit (0-31) from `value`, writing `true`/`false` to `*isSet`.
- `bitUtilsExtractBits(value, startBit, numBits, extractedBits)` - Extracts `numBits` bits starting at `startBit` from `value`, right-aligned, into `*extractedBits`.
- `bitUtilsInsertBits(value, bitsToInsert, startBit, numBits, updatedValue)` - Inserts the low `numBits` bits of `bitsToInsert` into `value` at `startBit`, writing the result to `*updatedValue`.

All functions return a `BitUtilsStatus_t`:

| Status | Meaning |
|---|---|
| `BITUTILS_SUCCESS` | Operation completed |
| `BITUTILS_ERROR_NULL_POINTER` | Output pointer was `NULL` |
| `BITUTILS_ERROR_INVALID_BIT_POSITION` | `bitPosition` >= 32 |
| `BITUTILS_ERROR_INVALID_START_BIT` | `startBit` >= 32 |
| `BITUTILS_ERROR_INVALID_NUM_BITS` | `numBits` is 0, or `startBit + numBits` > 32 |
| `BITUTILS_ERROR_INVALID_VALUE` | `bitsToInsert` (InsertBits only) exceeds what fits in `numBits` |

Bit position 0 is the LSB; bit 31 is the MSB. All operations work on `uint32_t`.

## 4. Error Handling Strategy

All input validation is performed inside the module — callers do not need to pre-validate bit positions, ranges, or output pointers. Every public function checks its arguments and returns a `BitUtilsStatus_t` before performing any computation:

- **Bit position / range checks**: `bitPosition` (single-bit ops) and `startBit`/`numBits` (range ops) are checked against the 32-bit word width before any shift occurs. This exists specifically to prevent undefined behavior: shifting by an amount >= the operand's bit width is undefined in C regardless of signedness, so these checks are a correctness requirement, not just a usability nicety.
- **NULL pointer checks**: every function that writes a result through an output pointer checks it for `NULL` first and returns `BITUTILS_ERROR_NULL_POINTER` rather than dereferencing it.
- **Value-fits-in-width checks**: `bitUtilsInsertBits` additionally validates that `bitsToInsert` fits within `numBits`, returning `BITUTILS_ERROR_INVALID_VALUE` if it would be silently truncated.
- **Fail-closed on error**: on any validation failure, the output parameter is left untouched and the function returns before performing the operation — callers can rely on the output being unmodified whenever the return value isn't `BITUTILS_SUCCESS`.
- **No exceptions, no dynamic error objects, no logging**: consistent with the "no dynamic allocation" and "portable" goals, errors are communicated purely through the `BitUtilsStatus_t` return value.

Internal helper functions (`validateBitPosition`, `validateBitRange`, `generateMask`) are `static` and centralize this validation so each public function stays a thin wrapper: validate, then operate.

## 5. Limitations

- Designed for fixed-width `uint32_t` values only; no support for arbitrary-length bit arrays or other word widths (e.g. 8/16/64-bit).
- No advanced atomic or concurrency-aware operations — reentrancy means safe for concurrent *independent* calls, not atomic read-modify-write across threads sharing the same value.
- `bitUtilsInsertBits`/`bitUtilsExtractBits` operate on a single contiguous bitfield per call; multi-field packing/unpacking is left to the caller.
- Validation adds a small, constant amount of branching to every call; this is a deliberate tradeoff of a few cycles for safety and is not expected to be significant relative to O(1) bit operations, but hasn't been benchmarked against a hypothetical unchecked "fast path" variant.
