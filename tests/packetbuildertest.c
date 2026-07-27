/******************************************************************************
 * @file   packetBuilderTest.c
 * @brief  Unit tests for the fixed-format packet builder (packetBuilder.h).
 *
 * @author Abhishek Doddagoudar
 *
 * @date   July 2026
 ******************************************************************************/

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "crc16.h"
#include "packetBuilder.h"
#include "packetParser.h"
#include "protocol.h"

static int totalTests = 0;
static int passedTests = 0;

#define TEST_ASSERT(cond, desc)                               \
    do                                                        \
    {                                                         \
        totalTests++;                                         \
        if (cond)                                             \
        {                                                     \
            printf("  PASS (line %d): %s\n", __LINE__, desc); \
            passedTests++;                                    \
        }                                                     \
        else                                                  \
        {                                                     \
            printf("  FAIL (line %d): %s\n", __LINE__, desc); \
        }                                                     \
    } while (0)

static void testBuildWithPayload(void)
{
    printf("build packet with payload:\n");

    uint8_t payload[] = {0x11, 0x22, 0x33, 0x44};
    uint8_t packet[PACKET_MIN_PACKET_SIZE + sizeof(payload)];
    size_t packetLength = 0;

    PacketBuilderStatus_t status =
        packetBuilderBuildPacket(0x10, payload, (uint16_t)sizeof(payload), packet, sizeof(packet), &packetLength);

    TEST_ASSERT(status == PACKET_BUILDER_SUCCESS, "well-formed build with payload succeeds");
    TEST_ASSERT(packetLength == sizeof(packet), "packetLength matches expected total size");
    TEST_ASSERT(packet[0] == PACKET_HEADER1, "byte 0 is PACKET_HEADER1");
    TEST_ASSERT(packet[1] == PACKET_HEADER2, "byte 1 is PACKET_HEADER2");
    TEST_ASSERT(((uint16_t)(packet[2] << 8) | packet[3]) == (PACKET_COMMAND_SIZE + sizeof(payload)),
                "Length field correctly encodes command + payload size");
    TEST_ASSERT(packet[4] == 0x10, "command byte is embedded correctly");
    TEST_ASSERT(memcmp(&packet[5], payload, sizeof(payload)) == 0, "payload bytes are copied correctly");

    uint16_t expectedCrc = 0;
    CRC16_Status_t crcSts = crc16Compute(&packet[2], PACKET_LENGTH_SIZE + PACKET_COMMAND_SIZE + sizeof(payload), &expectedCrc);

    TEST_ASSERT(crcSts == CRC16_SUCCESS,
                "independent CRC calculation succeeds");

    if (crcSts != CRC16_SUCCESS)
    {
        return;
    }

    uint16_t embeddedCrc = (uint16_t)((packet[packetLength - 2] << 8) | packet[packetLength - 1]);
    TEST_ASSERT(embeddedCrc == expectedCrc, "embedded CRC matches an independently computed CRC");
}

static void testBuildZeroLengthPayload(void)
{
    printf("build packet with zero-length payload:\n");

    uint8_t packet[PACKET_MIN_PACKET_SIZE];
    size_t packetLength = 0;

    PacketBuilderStatus_t status = packetBuilderBuildPacket(0x20, NULL, 0, packet, sizeof(packet), &packetLength);

    TEST_ASSERT(status == PACKET_BUILDER_SUCCESS, "NULL payload with payloadLength 0 succeeds");
    TEST_ASSERT(packetLength == PACKET_MIN_PACKET_SIZE, "packetLength equals PACKET_MIN_PACKET_SIZE");
    TEST_ASSERT(((uint16_t)(packet[2] << 8) | packet[3]) == PACKET_COMMAND_SIZE,
                "Length field is PACKET_COMMAND_SIZE for an empty payload");
}

static void testBuildMaxPayload(void)
{
    printf("build packet with maximum (255-byte) payload:\n");

    uint8_t payload[255];
    memset(payload, 0x7A, sizeof(payload));

    uint8_t packet[PACKET_MAX_PACKET_SIZE];
    size_t packetLength = 0;

    PacketBuilderStatus_t status =
        packetBuilderBuildPacket(0x30, payload, (uint16_t)sizeof(payload), packet, sizeof(packet), &packetLength);

    TEST_ASSERT(status == PACKET_BUILDER_SUCCESS, "build with maximum payload succeeds");
    TEST_ASSERT(packetLength == PACKET_MAX_PACKET_SIZE, "packetLength equals PACKET_MAX_PACKET_SIZE");
    TEST_ASSERT(memcmp(&packet[5], payload, sizeof(payload)) == 0, "maximum-size payload copied correctly");
}

static void testPayloadLengthTooLarge(void)
{
    printf("negative: payloadLength of 256 is rejected:\n");

    uint8_t payload[256];
    memset(payload, 0x00, sizeof(payload));

    uint8_t packet[PACKET_MAX_PACKET_SIZE + 1];
    size_t packetLength = 0;

    PacketBuilderStatus_t status =
        packetBuilderBuildPacket(0x40, payload, (uint16_t)sizeof(payload), packet, sizeof(packet), &packetLength);

    TEST_ASSERT(status == PACKET_BUILDER_ERROR_INVALID_LENGTH,
                "payloadLength exceeding PACKET_MAX_PAYLOAD_SIZE is rejected");
}

static void testNullPacketBuffer(void)
{
    printf("NULL packetBuffer pointer:\n");

    uint8_t payload[] = {0x01};
    size_t packetLength = 0;

    PacketBuilderStatus_t status =
        packetBuilderBuildPacket(0x50, payload, (uint16_t)sizeof(payload), NULL, 100, &packetLength);

    TEST_ASSERT(status == PACKET_BUILDER_ERROR_NULL_POINTER, "NULL packetBuffer is rejected rather than dereferenced");
}

static void testNullPacketLength(void)
{
    printf("NULL packetLength pointer:\n");

    uint8_t payload[] = {0x01};
    uint8_t packet[PACKET_MIN_PACKET_SIZE + sizeof(payload)];

    PacketBuilderStatus_t status =
        packetBuilderBuildPacket(0x60, payload, (uint16_t)sizeof(payload), packet, sizeof(packet), NULL);

    TEST_ASSERT(status == PACKET_BUILDER_ERROR_NULL_POINTER, "NULL packetLength is rejected rather than dereferenced");
}

static void testNullPayloadWithNonzeroLength(void)
{
    printf("NULL payload with nonzero payloadLength:\n");

    uint8_t packet[PACKET_MIN_PACKET_SIZE + 4];
    size_t packetLength = 0;

    PacketBuilderStatus_t status = packetBuilderBuildPacket(0x70, NULL, 4, packet, sizeof(packet), &packetLength);

    TEST_ASSERT(status == PACKET_BUILDER_ERROR_NULL_POINTER,
                "NULL payload with payloadLength > 0 is rejected rather than dereferenced");
}

static void testBufferExactlyMinimumSize(void)
{
    printf("packetBufferSize exactly matches the required size:\n");

    uint8_t payload[] = {0xAA, 0xBB};
    uint8_t packet[PACKET_MIN_PACKET_SIZE + sizeof(payload)];
    size_t packetLength = 0;

    PacketBuilderStatus_t status =
        packetBuilderBuildPacket(0x80, payload, (uint16_t)sizeof(payload), packet, sizeof(packet), &packetLength);

    TEST_ASSERT(status == PACKET_BUILDER_SUCCESS, "packetBufferSize exactly equal to the required size succeeds");
    TEST_ASSERT(packetLength == sizeof(packet), "packetLength equals the exact buffer size provided");
}

static void testBufferOneByteTooSmall(void)
{
    printf("negative: packetBufferSize one byte too small:\n");

    uint8_t payload[] = {0xAA, 0xBB};
    uint8_t packet[PACKET_MIN_PACKET_SIZE + sizeof(payload)];
    size_t packetLength = 0;

    PacketBuilderStatus_t status = packetBuilderBuildPacket(
        0x90, payload, (uint16_t)sizeof(payload), packet, sizeof(packet) - 1, &packetLength);

    TEST_ASSERT(status == PACKET_BUILDER_ERROR_BUFFER_TOO_SMALL,
                "packetBufferSize one byte short of required is rejected");
}

static void testBufferTooSmallLeavesPacketLengthUntouched(void)
{
    printf("negative: rejected build does not write packetLength:\n");

    uint8_t payload[] = {0x01, 0x02, 0x03};
    uint8_t packet[4]; /* deliberately far too small */
    size_t packetLength = 0xDEADBEEF;

    PacketBuilderStatus_t status =
        packetBuilderBuildPacket(0xA0, payload, (uint16_t)sizeof(payload), packet, sizeof(packet), &packetLength);

    TEST_ASSERT(status == PACKET_BUILDER_ERROR_BUFFER_TOO_SMALL, "undersized buffer is rejected");
    TEST_ASSERT(packetLength == 0xDEADBEEF, "packetLength is left untouched when the build fails");
}

static void testRoundTripThroughParser(void)
{
    printf("round trip: built packet parses back to the original inputs:\n");

    uint8_t payload[] = {0x99, 0x88, 0x77, 0x66, 0x55};
    uint8_t packet[PACKET_MIN_PACKET_SIZE + sizeof(payload)];
    size_t packetLength = 0;

    PacketBuilderStatus_t buildStatus =
        packetBuilderBuildPacket(0xB0, payload, (uint16_t)sizeof(payload), packet, sizeof(packet), &packetLength);
    TEST_ASSERT(buildStatus == PACKET_BUILDER_SUCCESS, "round-trip build succeeds");

    ParsedPacket_t parsed;
    PacketParserStatus_t parseStatus = packetParserParse(packet, packetLength, &parsed);

    TEST_ASSERT(parseStatus == PACKET_PARSER_SUCCESS, "built packet parses back successfully");
    TEST_ASSERT(parsed.commandId == 0xB0, "parsed commandId matches the command given to the builder");
    TEST_ASSERT(parsed.payloadLength == sizeof(payload), "parsed payloadLength matches the built payload size");
    TEST_ASSERT(memcmp(parsed.payload, payload, sizeof(payload)) == 0,
                "parsed payload bytes match the original payload given to the builder");
}

static void testRoundTripZeroPayload(void)
{
    printf("round trip: zero-length payload builds and parses back:\n");

    uint8_t packet[PACKET_MIN_PACKET_SIZE];
    size_t packetLength = 0;

    PacketBuilderStatus_t buildStatus = packetBuilderBuildPacket(0xC0, NULL, 0, packet, sizeof(packet), &packetLength);
    TEST_ASSERT(buildStatus == PACKET_BUILDER_SUCCESS, "round-trip build with zero-length payload succeeds");

    ParsedPacket_t parsed;
    PacketParserStatus_t parseStatus = packetParserParse(packet, packetLength, &parsed);

    TEST_ASSERT(parseStatus == PACKET_PARSER_SUCCESS, "built zero-payload packet parses back successfully");
    TEST_ASSERT(parsed.payloadLength == 0, "parsed payloadLength is 0");
    TEST_ASSERT(parsed.payload == NULL, "parsed payload pointer is NULL for an empty payload");
}

static void testDistinctCommandIds(void)
{
    printf("command byte correctness across boundary values:\n");

    uint8_t packetLow[PACKET_MIN_PACKET_SIZE];
    uint8_t packetHigh[PACKET_MIN_PACKET_SIZE];
    size_t lenLow = 0;
    size_t lenHigh = 0;

    PacketBuilderStatus_t statusLow = packetBuilderBuildPacket(0x00, NULL, 0, packetLow, sizeof(packetLow), &lenLow);
    PacketBuilderStatus_t statusHigh =
        packetBuilderBuildPacket(0xFF, NULL, 0, packetHigh, sizeof(packetHigh), &lenHigh);

    TEST_ASSERT(statusLow == PACKET_BUILDER_SUCCESS, "build with commandId 0x00 succeeds");
    TEST_ASSERT(packetLow[4] == 0x00, "commandId 0x00 is embedded correctly");
    TEST_ASSERT(statusHigh == PACKET_BUILDER_SUCCESS, "build with commandId 0xFF succeeds");
    TEST_ASSERT(packetHigh[4] == 0xFF, "commandId 0xFF is embedded correctly");
}

static void testReentrantBackToBackBuilds(void)
{
    printf("reentrancy: back-to-back builds into separate buffers don't interfere:\n");

    uint8_t payloadA[] = {0x01, 0x02};
    uint8_t payloadB[] = {0xF1, 0xF2, 0xF3};
    uint8_t packetA[PACKET_MIN_PACKET_SIZE + sizeof(payloadA)];
    uint8_t packetB[PACKET_MIN_PACKET_SIZE + sizeof(payloadB)];
    size_t lenA = 0;
    size_t lenB = 0;

    PacketBuilderStatus_t statusA =
        packetBuilderBuildPacket(0xD0, payloadA, (uint16_t)sizeof(payloadA), packetA, sizeof(packetA), &lenA);
    PacketBuilderStatus_t statusB =
        packetBuilderBuildPacket(0xD1, payloadB, (uint16_t)sizeof(payloadB), packetB, sizeof(packetB), &lenB);

    TEST_ASSERT(statusA == PACKET_BUILDER_SUCCESS, "first build succeeds");
    TEST_ASSERT(statusB == PACKET_BUILDER_SUCCESS, "second build succeeds");
    TEST_ASSERT(packetA[4] == 0xD0, "first buffer retains its own command byte");
    TEST_ASSERT(packetB[4] == 0xD1, "second buffer retains its own command byte, unaffected by the first build");
    TEST_ASSERT(memcmp(&packetA[5], payloadA, sizeof(payloadA)) == 0, "first buffer's payload is unaffected");
    TEST_ASSERT(memcmp(&packetB[5], payloadB, sizeof(payloadB)) == 0, "second buffer's payload is unaffected");
}

int main(void)
{
    printf("Running packetBuilder tests...\n\n");

    testBuildWithPayload();
    testBuildZeroLengthPayload();
    testBuildMaxPayload();
    testPayloadLengthTooLarge();
    testNullPacketBuffer();
    testNullPacketLength();
    testNullPayloadWithNonzeroLength();
    testBufferExactlyMinimumSize();
    testBufferOneByteTooSmall();
    testBufferTooSmallLeavesPacketLengthUntouched();
    testRoundTripThroughParser();
    testRoundTripZeroPayload();
    testDistinctCommandIds();
    testReentrantBackToBackBuilds();

    printf("\n%d / %d tests passed.\n", passedTests, totalTests);

    return (passedTests == totalTests) ? 0 : 1;
}

/**************************************** END OF packetBuilderTest.c ****************************************/