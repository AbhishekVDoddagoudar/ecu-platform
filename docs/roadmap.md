# ECU Platform Roadmap

## Vision

The objective of this project is to build an educational Automotive ECU software platform in Embedded C, inspired by AUTOSAR Classic architecture.

The project is not intended to be AUTOSAR compliant. Instead, it aims to simulate the responsibilities and interactions of major software layers found in a production Automotive ECU.

Each module will be developed incrementally with clean APIs, documentation, unit tests, and production-oriented coding practices.

---

# Development Roadmap

## Milestone 0 - Project Foundation
Status: Completed

- [x] Create GitHub repository
- [x] Define project structure
- [x] Configure CMake
- [x] Create documentation structure
- [x] Define coding guidelines
- [x] Define project roadmap

---

## Milestone 1 - Embedded Utility Library
Status: Completed

Modules

- [x] Bit Utilities
- [x] Ring Buffer
- [x] CRC16
- [x] Packet Parser
- [x] Packet Builder
- [x] Software Timer
- [x] Memory Pool
- [x] CRC32
- [x] Logger

Skills

- Pointer arithmetic
- Bit manipulation
- API design
- Error handling
- Modular programming
- CRC/checksum algorithms
- Callback-driven timer management
- Compile-time vs. runtime configuration trade-offs

---

## Milestone 2 - CAN Communication Stack

Modules

- [ ] CAN Driver
- [ ] CAN Interface
- [ ] CAN Frame Encoder
- [ ] CAN Frame Decoder
- [ ] Software Filters
- [ ] TX Queue
- [ ] RX Queue

Skills

- CAN protocol
- CAN FD
- Arbitration
- Frame parsing

---

## Milestone 3 - Embedded OS

Modules

- [ ] Scheduler
- [ ] Task Manager
- [ ] Queue
- [ ] Event Manager

Skills

- RTOS concepts
- Scheduling
- Synchronization
- ISR communication

Note: Software Timer was originally scoped under this milestone but was
implemented and released as part of Milestone 1 (v0.7.0) instead, as a
standalone embedded utility module rather than a full OS abstraction.
See Milestone 1 above.

---

## Milestone 4 - AUTOSAR Inspired BSW

Modules

- [ ] COM
- [ ] PDU Router
- [ ] ECU Manager
- [ ] Memory Interface
- [ ] NVM Manager

Skills

- Layered architecture
- Module interaction
- Software abstraction

---

## Milestone 5 - Diagnostics

Modules

- [ ] Diagnostic Communication Manager
- [ ] Diagnostic Event Manager
- [ ] UDS Service Handler

Skills

- UDS
- DTC handling
- Diagnostic sessions

---

## Milestone 6 - Software Quality

- [ ] Unit Tests
- [ ] Static Analysis
- [ ] Error Handling Improvements
- [ ] Doxygen Documentation
- [ ] Code Refactoring

Skills

- MISRA-oriented coding
- Software quality
- Maintainability

---

## Milestone 7 - Interview Ready

- [ ] Final project cleanup
- [ ] Complete documentation
- [ ] Release v1.0
- [ ] Resume update
- [ ] Portfolio preparation

---

# Future Enhancements

The following modules are not part of the initial roadmap but may be added later.

- [ ] LIN
- [ ] Ethernet
- [ ] Bootloader
- [ ] Flash Driver
- [ ] Configuration Manager
- [ ] CLI Debug Console
- [ ] OTA Simulation

---

# Engineering Principles

This project follows the following principles:

- Simplicity over cleverness.
- Readability over brevity.
- Modular design with clear interfaces.
- Reusable components.
- Minimal dynamic memory allocation.
- Defensive programming.
- Thorough documentation.
- Incremental development.
- Unit testing where applicable.
- Production-quality code over quick prototypes.

# Current Progress

Current Milestone

Milestone 1 - Embedded Utility Library (Completed)

Next Milestone

Milestone 2 - CAN Communication Stack

Next Module

CAN Driver

# Current Version

v0.10.0