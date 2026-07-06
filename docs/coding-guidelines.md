# Coding Guidelines

This document outlines the coding standards and best practices to be followed throughout the Automotive ECU Platform project.

## Naming Conventions

- Use `camelCase` for function names (e.g., `calculateEngineRPM()`, `initializeSensor()`)
- Use `UPPER_SNAKE_CASE` for constants and macros (e.g., `MAX_BUFFER_SIZE`, `SENSOR_TIMEOUT`)
- Use `snake_case` for file names and variables when appropriate for clarity

## Documentation

- Document all public APIs using Doxygen comments
- Include parameter descriptions, return values, and any potential errors in Doxygen blocks
- Private functions should have brief comments explaining their purpose
- Example format:
  ```c
  /**
   * @brief Calculates engine RPM from sensor input
   * @param sensorValue Raw sensor value from ADC
   * @return Calculated RPM value
   */
  uint32_t calculateEngineRPM(uint16_t sensorValue);
  ```

## Memory Management

- **Avoid dynamic memory** unless explicitly justified in code comments
- Prefer stack allocation and static buffers for embedded systems
- When dynamic allocation is necessary, document the allocation strategy and cleanup approach
- Use fixed-size arrays with compile-time bounds checking where possible

## Global Variables

- Minimize global variable usage
- Encapsulate state within modules using static variables when needed
- Globals should only be used for hardware registers or other truly global resources
- Prefer passing state through function parameters or struct members

## Data Types

- Use fixed-width integer types from `<stdint.h>`:
  - `uint8_t`, `uint16_t`, `uint32_t`, `uint64_t` for unsigned integers
  - `int8_t`, `int16_t`, `int32_t`, `int64_t` for signed integers
  - Avoid generic `int`, `long`, `unsigned` types
- Use appropriate types to avoid overflow/underflow and ensure portability

## File Organization

- **Public Headers**: Place in `include/` directory
  - Contain only public API declarations
  - Use header guards or `#pragma once`
  - Minimize exposure of internal details
- **Private Implementation**: Place in `src/` directory
  - Contain internal data structures and helper functions
  - Use static linkage for non-public symbols
  - Include private headers with clear naming convention (e.g., `*_private.h`)

## Code Style

- Use consistent indentation (spaces preferred, specify width in project setup)
- Keep lines reasonably short (80-100 characters recommended)
- Use meaningful variable and function names
- Add comments for complex logic or non-obvious behavior
