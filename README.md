# ECU Platform

## Overview

ECU Platform is an educational project that simulates the software architecture of an Automotive Electronic Control Unit (ECU), inspired by AUTOSAR Classic.

The purpose of this project is to understand embedded software architecture by implementing reusable modules in Embedded C.

---

## Current Modules

- Bit Utilities
- Ring Buffer
- CRC16
- Packet Parser
- Packet Builder
- Software Timer
- Memory Pool
- CRC32
- Logger

---

## Features

| Module | Summary |
|---|---|
| Bit Utilities | Set/clear/toggle/check individual bits, extract/insert bit fields |
| Ring Buffer | Fixed-capacity circular buffer, caller-owned, reentrant |
| CRC16 | CRC-16-CCITT-FALSE, one-shot and incremental computation, verify |
| Packet Parser | Parses the project's fixed-format wire packet, CRC-checked |
| Packet Builder | Builds the project's fixed-format wire packet, CRC-checked |
| Software Timer | Pool of one-shot/periodic callback-driven timers |
| Memory Pool | Fixed-size block allocator, no dynamic memory |
| CRC32 | CRC-32/ISO-HDLC, one-shot and incremental computation, verify |
| Logger | Pluggable-backend logging with runtime and compile-time severity filtering |

---

## Planned Modules

Milestone 2 - CAN Communication Stack:

- CAN Driver
- CAN Interface
- CAN Frame Encoder
- CAN Frame Decoder
- Software Filters
- TX Queue
- RX Queue

See `docs/roadmap.md` for the complete milestone plan beyond Milestone 2.

---

## Technologies

- Embedded C
- CMake
- Git
- Doxygen

---


## Build

- cmake -S . -B builds
- cmake --build builds/ --target all

## Run Tests

cd builds
- ./bitUtilsTest
- ./crc16Test
- ./packetParserTest
- ./ringBufferTest
- ./packetbuildertest
- ./swTimerTest
- ./memoryPoolTest
- ./crc32Test
- ./loggerTest
- ./loggerCompileTimeTest

---

## Current Version

v0.10.0

---

## License

MIT