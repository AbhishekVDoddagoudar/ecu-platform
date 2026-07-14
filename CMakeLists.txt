cmake_minimum_required(VERSION 3.10)

project(
    ECUPlatform
    VERSION 1.0
    LANGUAGES C
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_library(ecuplatform STATIC

    src/bitUtils.c
    src/crc16.c
    src/packetParser.c
    src/ringBuffer.c

)

target_include_directories(ecuplatform
    PUBLIC
        include
)

target_compile_options(ecuplatform PRIVATE

    -Wall
    -Wextra
    -Wpedantic

)

add_executable(bitUtilsTest
    tests/bitUtilsTest.c
)

target_link_libraries(bitUtilsTest
    PRIVATE ecuplatform
)

add_executable(crc16Test
    tests/crc16Test.c
)

target_link_libraries(crc16Test
    PRIVATE ecuplatform
)

add_executable(packetParserTest
    tests/packetParserTest.c
)

target_link_libraries(packetParserTest
    PRIVATE ecuplatform
)

add_executable(ringBufferTest
    tests/testRingBuffer.c
)

target_link_libraries(ringBufferTest
    PRIVATE ecuplatform
)