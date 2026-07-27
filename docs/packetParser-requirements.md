# Packet Parser Requirements

## Responsibilities

A packet parser's job is to answer one question: **given a buffer of bytes
that claims to be a packet, is it actually one, and if so, what does it
say?** It sits at the boundary between "raw bytes that arrived from
somewhere" and "structured data the application can act on." Its
responsibility is narrowly: validate framing, validate integrity, and
extract fields — nothing more.

It should **not**:
- Manage the transport (it doesn't read from UART/CAN/TCP, doesn't buffer
  partial packets, doesn't reassemble a packet split across multiple reads).
- Resynchronize a byte stream (finding the next valid header after garbage
  is a framing/transport concern, not a parsing concern).
- Act on the decoded command (dispatch, execute, respond — that's the
  layer above).
- Retain any memory of previous calls. Each call is independent; the
  parser has no business remembering "the last packet."

If I catch myself wanting to add any of the above to this module, that's a
sign the responsibility has crept beyond what "parsing" should mean.

## Inputs

The parser needs exactly enough information to interpret a *single,
already-delimited* chunk of bytes:

- A pointer to the raw byte buffer.
- The number of bytes actually available at that pointer (not the
  buffer's *capacity* — the number of bytes that are meaningfully
  populated, which may be less than the buffer's declared size).
- Somewhere to write the result. This is technically an output, but it's
  passed in as an input parameter (a pointer the caller provides), so it's
  worth listing here too — the parser needs a destination, not just a
  source.

Nothing else. No configuration, no mode flags, no callback — a single
packet in a single buffer is fully self-describing once you know where it
starts and how many bytes you're allowed to read.

## Outputs

What a caller needs after a successful parse, thinking purely from "what
would I do with this result":

- A status/result code — did it succeed, and if not, why.
- The Command ID — what this packet is telling the application to do.
- The Payload — the data associated with that command.
- The Payload length — you can't safely use the payload without knowing
  how much of it there is.

That's the full output set for a *successful* parse. On failure, I'd argue
the only thing that matters is the status code — I don't want partially
populated output on failure, because a caller that forgets to check the
status code and reads garbage fields is a bug waiting to happen.

## Error Conditions

Thinking from first principles about everything that could be wrong with
an incoming buffer, before assuming any particular implementation:

1. The buffer pointer itself is NULL.
2. The output-destination pointer is NULL.
3. The buffer length is 0, or too small to contain even the smallest
   possible valid packet.
4. The first header byte doesn't match what's expected.
5. The second header byte doesn't match what's expected.
6. The declared length field is logically impossible (e.g. zero, when a
   command byte must always be present).
7. The declared length field claims more data than the buffer actually
   contains — the packet is truncated.
8. The declared length field exceeds the maximum this parser (or this
   payload's storage) can ever support, regardless of what the buffer
   contains.
9. The CRC computed over the received bytes doesn't match the CRC field
   in the packet — corruption or a sender-side bug.
10. The buffer contains *more* bytes than the declared packet needs —
    either trailing garbage, or a second packet appended right after the
    first, which is ambiguous unless the parser explicitly defines
    whether trailing bytes are an error or simply ignored.
11. Arithmetic that computes "total packet size" from the declared length
    could in principle overflow if the length field's range weren't
    tightly bounded — worth checking that the max declared length can
    never make that computation wrap.
12. A single buffer accidentally containing multiple valid-looking
    packets back to back (a framing question upstream of this module, but
    worth naming as something that could confuse a caller who assumes
    "one buffer, one packet").

That's 12, more than the ask of 10 — some of these (10, 12) are really the
same underlying ambiguity ("what if there's more than one packet's worth
of data here") viewed from two angles, but I think they're both worth
naming since they'd surface differently depending on how the module is
used.

## Packet Layout

```
Offset:   0        1        2   3        4        5 ... (5+N-1)   (5+N) (6+N)
        +--------+--------+--------+--------+-----------------+--------+--------+
        | Header1| Header2|      Length     | Command|     Payload     |     CRC16       |
        |  0xAA  |  0x55  |    2 bytes      | 1 byte |    N bytes      |    2 bytes      |
        +--------+--------+--------+--------+-----------------+--------+--------+
                            (big-endian)                        (big-endian)
```

- **Header1 (1 byte)** and **Header2 (1 byte)**: fixed sentinel values.
  Not covered by the CRC — they exist purely to let a receiver recognize
  "a packet probably starts here," so it would be circular to protect
  them with a checksum computed *after* you've already decided this is a
  packet.
- **Length (2 bytes)**: counts only Command + Payload. Not itself
  included in its own count, and header bytes aren't counted either.
  2 bytes because Command (1) + max Payload (255) can total 256, which
  doesn't fit in a single byte.
- **Command (1 byte)**: the identifier the application dispatches on.
- **Payload (0–255 bytes)**: variable-length, application-defined.
- **CRC16 (2 bytes)**: covers Length + Command + Payload. Explicitly
  excludes the header bytes.

Total packet size = `2 (header) + 2 (length) + 1 (command) + N (payload) +
2 (crc)` = `7 + N` bytes, so 7 bytes minimum (empty payload) up to 262
bytes maximum (255-byte payload).

## Memory Ownership

Given:
```c
uint8_t rxBuffer[256];
packetParserParse(rxBuffer, ...);
```

**Option A (copy payload into new storage)** vs. **Option B (return a
pointer into `rxBuffer`)** — I'd choose based on one question: *is
`rxBuffer` guaranteed to stay unchanged for as long as the caller needs
the parsed result?*

- If yes — e.g. a synchronous, single-threaded flow where the caller
  reads a packet, parses it, uses the result, and only then goes back to
  read the next one — **Option B is better**. It's free: no extra memory,
  no copy cost, and it satisfies "no dynamic allocation" trivially since
  there's nothing to allocate. The tradeoff is that the parsed payload's
  validity is now tied to the original buffer's lifetime, and that
  contract has to be documented loudly, because it's easy to forget.

- If no — e.g. `rxBuffer` is a DMA target that an ISR refills
  asynchronously, or a double-buffer that gets swapped out from under the
  application shortly after the interrupt fires — **Option A becomes the
  safer choice**, even though it costs a copy. A dangling-or-moved-buffer
  bug in that scenario is the kind of intermittent, timing-dependent bug
  that's brutal to track down in embedded systems, and the cost of
  copying up to 255 bytes is genuinely negligible next to that risk.

So the "right" answer isn't fixed — it depends on what surrounds the
parser, specifically on who else might touch `rxBuffer` and when. That's
worth pinning down explicitly before locking in the API, rather than
picking one and hoping it fits every caller.

## Extensibility Considerations

Today's format is `Header | Length | Command | Payload | CRC`. If a future
version adds `Version | Flags | Sequence Number` before `Command`, would
today's design survive that without a full rewrite?

Honestly — only partially, and only if a few things are done deliberately
now rather than later:

- **Every field offset needs to be a named constant, derived from the
  previous field's offset plus its size — never a hardcoded magic
  number.** If `Command`'s offset is written as a raw `4` in five
  different places, adding a field before it means hunting down and
  fixing five numbers by hand, which is exactly the kind of change that's
  easy to get wrong in one spot.
- **The "read a multi-byte field out of raw bytes" logic should be one
  small, reusable piece**, not duplicated per field. Adding
  `SequenceNumber` as a new 2-byte field should mean calling that same
  helper again, not writing new inline byte-assembly logic.
- **The CRC's coverage (which byte range it protects) should be an
  explicit, named quantity** — a start offset and a length computed from
  the current field layout — not an inline expression buried in the
  parsing logic. If a new version changes what the CRC covers, that
  should be a one-line change, not a hunt through the function.
- The deeper risk: today's design bundles "how to read this specific
  layout" and "the order of validation checks" into one function. If the
  format genuinely needs to change field-by-field over time, at some
  point it may be worth separating "how to pull fields out of *a* layout"
  from "which layout are we even looking at" — because a `Version` field
  specifically raises the question of whether the parser needs to
  support *multiple* layouts side by side (old packets still arriving
  from devices that haven't been updated), not just one evolving layout.
  That's a materially bigger design question than "shift some offsets,"
  and I don't think it should be solved preemptively — but it's worth
  naming now so it isn't a surprise later.