# packetParser Design

This document builds directly on `docs/packetParser-requirements.md`. Where
that document left a question open, this document makes the call and
explains why.

## 1. Problem Statement

The packetParser module validates and decodes a single, already-delimited
packet from a raw byte buffer: it checks framing (header bytes), checks
that the declared length is both internally consistent and actually backed
by the bytes available, verifies data integrity (CRC-16-CCITT-FALSE), and
exposes the decoded Command ID and Payload to the caller in structured
form. It sits beneath higher-level protocol handlers (transport reassembly,
command dispatch) that already have one packet's worth of bytes in hand and
need it validated before acting on it.

Per the requirements doc's Responsibilities section, this module explicitly
does **not** manage transport, does not resynchronize a byte stream, and
does not act on the decoded command.

## 2. Packet Format

```
Offset:   0        1        2   3        4        5 ... (5+N-1)   (5+N) (6+N)
        +--------+--------+--------+--------+-----------------+--------+--------+
        | Header1| Header2|      Length     | Command|     Payload     |     CRC16       |
        |  0xAA  |  0x55  |    2 bytes (BE) | 1 byte |    N bytes      |  2 bytes (BE)   |
        +--------+--------+--------+--------+-----------------+--------+--------+
```

- **Header1 / Header2**: fixed sentinels, `0xAA` / `0x55`. Not covered by
  the CRC (see Requirements: it would be circular to CRC-protect the bytes
  that tell you whether this is a packet worth CRC-checking at all).
- **Length** (big-endian `uint16_t`): counts Command + Payload only.
  Range: 1 to 256.
- **Command**: 1-byte identifier.
- **Payload**: 0 to 255 bytes.
- **CRC16** (big-endian `uint16_t`): CRC-16-CCITT-FALSE over
  Length + Command + Payload.

Total on-wire size = `7 + payloadLength` bytes (7 to 262).

**Trailing-bytes policy** (resolves Requirements error items #10/#12): the
parser reads exactly `7 + decodedLength` bytes starting at offset 0 of
whatever buffer it's given. If the caller's buffer contains more bytes than
that (e.g. it's a fixed-size receive buffer, or a second packet happens to
follow), those extra bytes are simply not looked at and do not cause an
error. `rawLen` means "at least this many valid bytes are available," not
"exactly one packet's worth is present." Detecting or splitting multiple
concatenated packets in one buffer is explicitly a transport/framing
concern above this module, consistent with the Responsibilities section.

**Overflow safety** (resolves Requirements error item #11): the maximum
possible `decodedLength` is 256 (bounded by the 2-byte Length field's valid
range as defined by this format), so `totalPacketSize = 7 + decodedLength`
tops out at 263 -- nowhere close to overflowing `size_t` on any realistic
target. No explicit overflow check is needed as long as `decodedLength`'s
upper bound is validated before the size computation, which it is.

## 3. Design Goals

- Reusable across any transport that can hand the parser a contiguous byte
  buffer.
- No dynamic allocation.
- Reentrant: no internal or global state.
- Platform independent: explicit big-endian wire format regardless of host
  endianness.
- Fail-safe: every check happens before any field is trusted or exposed.

## 4. Parsed Packet Representation

```c
typedef struct
{
    uint8_t commandId;
    uint8_t payloadLength;
    const uint8_t *payload;
} ParsedPacket_t;
```

## 5. Memory Ownership Decision

The requirements doc left this conditional on one question: *is the
caller's buffer guaranteed to stay unchanged for as long as the parsed
result is needed?*

**Decision for this module: Option B -- zero-copy, `payload` points into
the caller's original buffer.** Rationale:

- This module has no visibility into or control over the transport layer
  above it, so it cannot guarantee buffer stability either way -- the
  contract has to be explicit regardless of which option is chosen.
- Given the "no dynamic allocation" goal and this project's general
  embedded/synchronous processing model, zero-copy is the lower-cost
  default, and callers with an asynchronous/DMA receive path (where the
  source buffer might be overwritten before the result is consumed) are
  expected to copy the buffer themselves *before* calling
  `packetParserParse()` if their transport requires it. That copy decision
  belongs to the transport layer, which knows its own buffer lifetime
  rules -- this module shouldn't guess.
- This is documented as a hard requirement on the caller, not a hidden
  assumption: `payload` is only valid as long as `rawData` is valid and
  unmodified.

If a future caller's transport genuinely can't satisfy that constraint,
the right fix is a copy at that call site (or a second, explicitly-named
copying variant of this API later) -- not silently making every caller pay
for a copy they may not need.

## 6. Error Codes

Mapping the requirements doc's first-principles error list onto a small,
practical set of status codes (same "not too many, not too few" philosophy
used in `bitUtils`/`crc16`/`ringBuffer`):

| Status | Covers (from requirements doc) |
|---|---|
| `PACKET_PARSER_SUCCESS` | -- |
| `PACKET_PARSER_ERROR_NULL_POINTER` | #1 (NULL buffer), #2 (NULL output) |
| `PACKET_PARSER_ERROR_INVALID_LENGTH` | #3 (buffer too small), #6 (length field logically impossible), #7 (declared length exceeds buffer), #8 (declared length exceeds max supported) |
| `PACKET_PARSER_ERROR_INVALID_HEADER` | #4, #5 (header byte mismatch) |
| `PACKET_PARSER_ERROR_CRC_MISMATCH` | #9 (CRC mismatch) |

Items #10-#12 (trailing bytes, arithmetic overflow, multiple packets) are
resolved by design policy above rather than by a status code -- they are
not caller-facing errors once the trailing-bytes and overflow-safety
decisions are made explicit.

Four distinct failure codes, not one per requirements-doc bullet: several
first-principles error conditions collapse to the same caller action
("reject this packet, don't trust any field in it"), so splitting them
further wouldn't give a caller anything actionable to do differently.

## 7. Extensibility

Per the requirements doc's Extensibility Considerations, three things are
committed to now so a future field addition (e.g. `Version`, `Flags`,
`Sequence Number` inserted before `Command`) doesn't require a rewrite:

1. Every field offset and size is a **named constant**, computed from the
   previous field's offset and size -- never a bare number repeated inline.
2. Multi-byte field assembly (endianness handling) is a **single reusable
   helper**, reused for both the Length and CRC fields today, and reusable
   for any future multi-byte field without new logic.
3. The CRC's covered byte range is an **explicit, named quantity**
   (offset + length), not inline arithmetic buried in the validation flow.

This does **not** solve the harder question the requirements doc raised:
if a future `Version` field means old-format packets must still be
readable from not-yet-upgraded devices, that's a "support multiple
layouts" problem, not a "shift some offsets" problem. This design
deliberately does not attempt to solve that preemptively -- doing so now
would mean guessing at a shape for a requirement that doesn't exist yet.

## 8. Dependencies

- **bitUtils**: `bitUtilsInsertBits()` assembles the big-endian 16-bit
  Length and CRC fields from individual bytes.
- **crc16**: `crc16Verify()` validates the CRC-16-CCITT-FALSE checksum over
  the Length + Command + Payload region.

## 9. Limitations

- Fixed packet format; not runtime-configurable.
- No stream framing/resynchronization (see Trailing-bytes policy above).
- Zero-copy payload means the caller owns buffer lifetime; see Memory
  Ownership Decision.