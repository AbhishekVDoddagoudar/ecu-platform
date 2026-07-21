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

---

## Features

### Bit Utility
- Set bit
- Clear bit
- Toggle bit
- Check if bit is set
- Extract bits
- Insert bits

---

## Planned Modules

- CAN Driver
- CAN Interface
- PDU Router
- COM Module
- Diagnostic Communication Manager
- Diagnostic Event Manager
- ECU State Manager
- Memory Manager

---

## Technologies

- Embedded C
- CMake
- Git
- Doxygen

---


## Build

cmake -S . -B builds
cmake --build builds/ --target all

## Run Tests

cd builds
./bitUtilsTest 
./crc16Test
./ringBufferTest
./packetParserTest

---

## License

MIT