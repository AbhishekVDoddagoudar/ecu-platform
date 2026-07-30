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
    src/packetBuilder.c
    src/swTimer.c

)

target_include_directories(ecuplatform
    PUBLIC
        include
)

target_compile_options(ecuplatform PRIVATE

    -Wall
    -Wextra
    -Wpedantic
    -Werror

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
    tests/ringBufferTest.c
)

target_link_libraries(ringBufferTest
    PRIVATE ecuplatform
)

add_executable(packetbuildertest
    tests/packetbuildertest.c
)

target_link_libraries(packetbuildertest
    PRIVATE ecuplatform
)


add_executable(swTimerTest
    tests/swTimerTest.c
)

target_link_libraries(swTimerTest
    PRIVATE ecuplatform
)

