/******************************************************************************
 * @file   packetParserTest.c
 * @brief  Unit tests for the fixed-format packet parser (packetParser.h).
 *
 * @author Abhishek Doddagoudar
 *
 * @date   July 2026
 ******************************************************************************/

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "crc16.h"
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

/**
 * @brief Build a well-formed packet frame into buf.
 *
 * @details Layout: [hdr1][hdr2][length_hi][length_lo][command][payload...][crc_hi][crc_lo].
 *          The Length field covers command + payload only. The CRC is
 *          computed via crc16Compute itself over Length + Command + Payload,
 *          so this helper produces a self-consistent valid packet regardless
 *          of the CRC algorithm's internals.
 *
 * @param buf        Destination buffer, must be at least
 *                    (PACKET_MIN_PACKET_SIZE + payloadLen) bytes.
 * @param commandId  Command byte to embed.
 * @param payload    Payload bytes to embed (may be NULL if payloadLen is 0).
 * @param payloadLen Number of payload bytes.
 *
 * @return size_t Total packet length written (PACKET_MIN_PACKET_SIZE + payloadLen).
 */
static size_t buildValidPacket(uint8_t *buf, uint8_t commandId, const uint8_t *payload, uint8_t payloadLen)
{
    buf[0] = PACKET_HEADER1;
    buf[1] = PACKET_HEADER2;

    uint16_t declaredLength = (uint16_t)(PACKET_COMMAND_SIZE + payloadLen);
    buf[2] = (uint8_t)(declaredLength >> 8);
    buf[3] = (uint8_t)(declaredLength & 0xFF);

    buf[4] = commandId;

    if ((payloadLen > 0U) && (payload != NULL))
    {
        memcpy(&buf[5], payload, payloadLen);
    }

    size_t crcSpan = (size_t)PACKET_LENGTH_SIZE + declaredLength;

    uint16_t crc = 0;
    CRC16_Status_t crcSts = crc16Compute(&buf[2], crcSpan, &crc);

    if (crcSts != CRC16_SUCCESS)
    {
        return CRC16_ERROR_MISMATCH;
    }

    buf[5 + payloadLen] = (uint8_t)(crc >> 8);
    buf[6 + payloadLen] = (uint8_t)(crc & 0xFF);

    return (size_t)PACKET_MIN_PACKET_SIZE + payloadLen;
}

static void testValidPacketWithPayload(void)
{
    printf("valid packet with payload:\n");

    uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint8_t buf[PACKET_MIN_PACKET_SIZE + sizeof(payload)];
    size_t len = buildValidPacket(buf, 0x01, payload, (uint8_t)sizeof(payload));

    ParsedPacket_t parsed;
    memset(&parsed, 0, sizeof(parsed));

    PacketParserStatus_t status = packetParserParse(buf, len, &parsed);

    TEST_ASSERT(status == PACKET_PARSER_SUCCESS, "well-formed packet with payload parses OK");
    TEST_ASSERT(parsed.commandId == 0x01, "parsed commandId matches input command");
    TEST_ASSERT(parsed.payloadLength == sizeof(payload), "parsed payloadLength matches input payload size");
    TEST_ASSERT(parsed.payload != NULL, "parsed payload pointer is non-NULL");
    TEST_ASSERT(memcmp(parsed.payload, payload, sizeof(payload)) == 0,
                "parsed payload bytes match the original payload");
}

static void testValidPacketZeroLengthPayload(void)
{
    printf("valid packet with zero-length payload:\n");

    uint8_t buf[PACKET_MIN_PACKET_SIZE];
    size_t len = buildValidPacket(buf, 0x02, NULL, 0);

    ParsedPacket_t parsed;
    memset(&parsed, 0, sizeof(parsed));

    PacketParserStatus_t status = packetParserParse(buf, len, &parsed);

    TEST_ASSERT(status == PACKET_PARSER_SUCCESS, "packet with zero-length payload parses OK");
    TEST_ASSERT(parsed.commandId == 0x02, "parsed commandId matches input command for empty-payload packet");
    TEST_ASSERT(parsed.payloadLength == 0, "parsed payloadLength is 0 for an empty payload");
    TEST_ASSERT(parsed.payload == NULL, "parsed payload pointer is NULL for an empty payload");
}

static void testPacketTooShort(void)
{
    printf("packet shorter than minimum length:\n");

    /* PACKET_MIN_PACKET_SIZE is 7; this buffer is only 4 bytes. */
    uint8_t buf[4] = {PACKET_HEADER1, PACKET_HEADER2, 0x00, 0x00};
    ParsedPacket_t parsed;

    PacketParserStatus_t status = packetParserParse(buf, sizeof(buf), &parsed);

    TEST_ASSERT(status == PACKET_PARSER_ERROR_INVALID_LENGTH,
                "packet shorter than PACKET_MIN_PACKET_SIZE is rejected as invalid length");
}

static void testInvalidHeader(void)
{
    printf("packet with wrong header bytes:\n");

    uint8_t payload[] = {0x01, 0x02};
    uint8_t buf[PACKET_MIN_PACKET_SIZE + sizeof(payload)];
    size_t len = buildValidPacket(buf, 0x03, payload, (uint8_t)sizeof(payload));

    /* Corrupt the first header byte so it no longer matches PACKET_HEADER1. */
    buf[0] ^= 0xFF;

    ParsedPacket_t parsed;
    PacketParserStatus_t status = packetParserParse(buf, len, &parsed);

    TEST_ASSERT(status == PACKET_PARSER_ERROR_INVALID_HEADER, "packet with corrupted header bytes is rejected");
}

static void testLengthFieldMismatch(void)
{
    printf("packet length field does not match actual buffer length:\n");

    uint8_t payload[] = {0x01, 0x02, 0x03};
    uint8_t buf[PACKET_MIN_PACKET_SIZE + sizeof(payload)];
    size_t len = buildValidPacket(buf, 0x04, payload, (uint8_t)sizeof(payload));

    /* Claim a much larger declared Length than what the buffer actually
     * contains, without resizing the buffer or recomputing the CRC -- this
     * must be caught by the rawLength-vs-declared-size check, independent
     * of the CRC check. */
    uint16_t badLength = (uint16_t)(PACKET_COMMAND_SIZE + sizeof(payload) + 50U);
    buf[2] = (uint8_t)(badLength >> 8);
    buf[3] = (uint8_t)(badLength & 0xFF);

    ParsedPacket_t parsed;
    PacketParserStatus_t status = packetParserParse(buf, len, &parsed);

    TEST_ASSERT(status == PACKET_PARSER_ERROR_INVALID_LENGTH,
                "mismatch between the embedded Length field and actual rawLength is rejected");
}

static void testInvalidCrc(void)
{
    printf("packet with corrupted CRC:\n");

    uint8_t payload[] = {0x10, 0x20, 0x30};
    uint8_t buf[PACKET_MIN_PACKET_SIZE + sizeof(payload)];
    size_t len = buildValidPacket(buf, 0x05, payload, (uint8_t)sizeof(payload));

    /* Flip a bit in the payload after the CRC has already been computed and
     * embedded, so the CRC no longer matches the (now-different) contents. */
    buf[5] ^= 0x01;

    ParsedPacket_t parsed;
    PacketParserStatus_t status = packetParserParse(buf, len, &parsed);

    TEST_ASSERT(status == PACKET_PARSER_ERROR_CRC_MISMATCH, "packet with mismatched CRC is rejected");
}

static void testByteSwappedHeaderRejected(void)
{
    printf("negative: byte-swapped header bytes are rejected:\n");

    /* Deliberately writes the two header bytes in the wrong order -- this is
     * the classic endianness-confusion bug: 0xAA55 stored as {0x55, 0xAA}
     * instead of the correct {0xAA, 0x55}. Must be rejected as an invalid
     * header, not accidentally accepted. */
    uint8_t payload[] = {0x01};
    uint8_t buf[PACKET_MIN_PACKET_SIZE + sizeof(payload)];
    size_t len = buildValidPacket(buf, 0x06, payload, (uint8_t)sizeof(payload));

    uint8_t tmp = buf[0];
    buf[0] = buf[1];
    buf[1] = tmp;

    ParsedPacket_t parsed;
    PacketParserStatus_t status = packetParserParse(buf, len, &parsed);

    TEST_ASSERT(status == PACKET_PARSER_ERROR_INVALID_HEADER, "byte-swapped header (endianness mix-up) is rejected");
}

static void testCrcFieldItselfCorrupted(void)
{
    printf("negative: corrupting only the trailing CRC field (payload untouched) is rejected:\n");

    /* Distinguishes "payload changed, so its true CRC no longer matches the
     * embedded one" (already covered by testInvalidCrc) from "payload is
     * completely correct, but the CRC bytes on the wire were corrupted in
     * transit" -- both must be rejected the same way. */
    uint8_t payload[] = {0xAB, 0xCD};
    uint8_t buf[PACKET_MIN_PACKET_SIZE + sizeof(payload)];
    size_t len = buildValidPacket(buf, 0x07, payload, (uint8_t)sizeof(payload));

    buf[len - 1] ^= 0xFF; /* flip the low CRC byte only */

    ParsedPacket_t parsed;
    PacketParserStatus_t status = packetParserParse(buf, len, &parsed);

    TEST_ASSERT(status == PACKET_PARSER_ERROR_CRC_MISMATCH,
                "corrupted trailing CRC byte is rejected even with a valid payload");
}

static void testZeroLengthBuffer(void)
{
    printf("negative: zero-length packet buffer is rejected:\n");

    uint8_t dummy[1] = {0};
    ParsedPacket_t parsed;
    PacketParserStatus_t status = packetParserParse(dummy, 0, &parsed);

    TEST_ASSERT(status == PACKET_PARSER_ERROR_INVALID_LENGTH, "a zero-length packet is rejected as invalid length");
}

static void testNullPacketPointer(void)
{
    printf("NULL rawData pointer:\n");

    ParsedPacket_t parsed;
    /* rawLength is >= PACKET_MIN_PACKET_SIZE here so the NULL check is what
     * actually triggers the rejection, rather than the length check. */
    PacketParserStatus_t status = packetParserParse(NULL, PACKET_MIN_PACKET_SIZE, &parsed);

    TEST_ASSERT(status == PACKET_PARSER_ERROR_NULL_POINTER, "NULL rawData pointer is rejected rather than dereferenced");
}

static void testNullParsedPktPointer(void)
{
    printf("NULL parsedPacket pointer:\n");

    uint8_t payload[] = {0x01};
    uint8_t buf[PACKET_MIN_PACKET_SIZE + sizeof(payload)];
    size_t len = buildValidPacket(buf, 0x08, payload, (uint8_t)sizeof(payload));

    PacketParserStatus_t status = packetParserParse(buf, len, NULL);

    TEST_ASSERT(status == PACKET_PARSER_ERROR_NULL_POINTER,
                "NULL parsedPacket pointer is rejected rather than dereferenced");
}

static void testMaxPayloadLength(void)
{
    printf("maximum payload length (payloadLength == 255):\n");

    uint8_t payload[255];
    memset(payload, 0x42, sizeof(payload));

    uint8_t buf[PACKET_MIN_PACKET_SIZE + sizeof(payload)];
    size_t len = buildValidPacket(buf, 0x09, payload, (uint8_t)sizeof(payload));

    ParsedPacket_t parsed;
    PacketParserStatus_t status = packetParserParse(buf, len, &parsed);

    TEST_ASSERT(status == PACKET_PARSER_SUCCESS, "packet with maximum (255-byte) payload parses OK");
    TEST_ASSERT(parsed.payloadLength == 255, "parsed payloadLength correctly reports 255");
    TEST_ASSERT(parsed.payload != NULL, "parsed payload pointer is non-NULL at maximum payload size");
}

static void testGetPacketSize(void)
{
    printf("packetParserGetPacketSize peek:\n");

    uint8_t payload[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    uint8_t buf[PACKET_MIN_PACKET_SIZE + sizeof(payload)];
    size_t len = buildValidPacket(buf, 0x0A, payload, (uint8_t)sizeof(payload));

    size_t peekedSize = packetParserGetPacketSize(buf);
    TEST_ASSERT(peekedSize == len, "packetParserGetPacketSize matches the actual total packet size");

    size_t nullSize = packetParserGetPacketSize(NULL);
    TEST_ASSERT(nullSize == 0, "packetParserGetPacketSize returns 0 for a NULL rawData pointer");
}

int main(void)
{
    printf("Running packetParser tests...\n\n");

    testValidPacketWithPayload();
    testValidPacketZeroLengthPayload();
    testPacketTooShort();
    testInvalidHeader();
    testLengthFieldMismatch();
    testInvalidCrc();
    testByteSwappedHeaderRejected();
    testCrcFieldItselfCorrupted();
    testZeroLengthBuffer();
    testNullPacketPointer();
    testNullParsedPktPointer();
    testMaxPayloadLength();
    testGetPacketSize();

    printf("\n%d / %d tests passed.\n", passedTests, totalTests);

    return (passedTests == totalTests) ? 0 : 1;
}

/**************************************** END OF packetParserTest.c ****************************************/