/******************************************************************************
 * @file   packetParserTest.c
 * @brief  Unit tests for SimplePkt packet parsing (packetParser.h).
 *
 * @author Abhishek Doddagoudar
 *
 * @date   July 2026
 ******************************************************************************/

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <packetParser.h>

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
 * @brief Build a well-formed SimplePkt frame into buf.
 *
 * Layout: [hdr_hi][hdr_lo][payLoadLen][payload...][crc_hi][crc_lo]
 * The CRC is computed via crc16Calculate itself over header+len+payload,
 * so this helper produces a self-consistent valid packet regardless of the
 * exact numeric value of SIMPLEPKT_HDR or the CRC algorithm's internals.
 *
 * @param buf Destination buffer, must be at least (5 + payloadLen) bytes.
 * @param payload Payload bytes to embed (may be NULL if payloadLen is 0).
 * @param payloadLen Number of payload bytes.
 * @return Total packet length written (5 + payloadLen).
 */
static size_t buildValidPacket(uint8_t *buf, const uint8_t *payload, uint8_t payloadLen)
{
    buf[0] = (uint8_t)(SIMPLEPKT_HDR >> 8);
    buf[1] = (uint8_t)(SIMPLEPKT_HDR & 0xFF);
    buf[2] = payloadLen;

    if (payloadLen > 0 && payload != NULL)
    {
        memcpy(&buf[3], payload, payloadLen);
    }

    size_t crcSpan = (size_t)(3 + payloadLen);
    uint16_t crc = crc16Calculate(buf, crcSpan);

    buf[3 + payloadLen] = (uint8_t)(crc >> 8);
    buf[4 + payloadLen] = (uint8_t)(crc & 0xFF);

    return (size_t)(5 + payloadLen);
}

static void testValidPacketWithPayload(void)
{
    printf("valid packet with payload:\n");

    uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint8_t buf[5 + sizeof(payload)];
    size_t len = buildValidPacket(buf, payload, (uint8_t)sizeof(payload));

    ParsedExamplePkt_t parsed;
    memset(&parsed, 0, sizeof(parsed));

    PacketStatus_t status = parseExamplePkt(buf, len, &parsed);

    TEST_ASSERT(status == PACKET_STATUS_OK, "well-formed packet with payload parses OK");
    TEST_ASSERT(parsed.payLoadLen == sizeof(payload), "parsed payLoadLen matches input payload size");
    TEST_ASSERT(parsed.isCkSumValid == true, "parsed isCkSumValid is true on a valid packet");
    TEST_ASSERT(parsed.payLoad != NULL, "parsed payLoad pointer is non-NULL");
    TEST_ASSERT(memcmp(parsed.payLoad, payload, sizeof(payload)) == 0,
                "parsed payLoad bytes match the original payload");
}

static void testValidPacketZeroLengthPayload(void)
{
    printf("valid packet with zero-length payload:\n");

    uint8_t buf[5];
    size_t len = buildValidPacket(buf, NULL, 0);

    ParsedExamplePkt_t parsed;
    memset(&parsed, 0, sizeof(parsed));

    PacketStatus_t status = parseExamplePkt(buf, len, &parsed);

    TEST_ASSERT(status == PACKET_STATUS_OK, "packet with zero-length payload parses OK");
    TEST_ASSERT(parsed.payLoadLen == 0, "parsed payLoadLen is 0 for an empty payload");
    TEST_ASSERT(parsed.isCkSumValid == true, "parsed isCkSumValid is true on a valid empty-payload packet");
}

static void testPacketTooShort(void)
{
    printf("packet shorter than minimum length:\n");

    uint8_t buf[4] = {0xAA, 0x55, 0x00, 0x00}; /* only 4 bytes, minimum is 5 */
    ParsedExamplePkt_t parsed;

    PacketStatus_t status = parseExamplePkt(buf, sizeof(buf), &parsed);

    TEST_ASSERT(status == PACKET_STATUS_INVALID_LENGTH, "packet shorter than 5 bytes is rejected as invalid length");
}

static void testInvalidHeader(void)
{
    printf("packet with wrong header bytes:\n");

    uint8_t payload[] = {0x01, 0x02};
    uint8_t buf[5 + sizeof(payload)];
    size_t len = buildValidPacket(buf, payload, (uint8_t)sizeof(payload));

    /* Corrupt the header so it no longer matches SIMPLEPKT_HDR. */
    buf[0] ^= 0xFF;

    ParsedExamplePkt_t parsed;
    PacketStatus_t status = parseExamplePkt(buf, len, &parsed);

    TEST_ASSERT(status == PACKET_STATUS_INVALID_HEADER, "packet with corrupted header bytes is rejected");
}

static void testLengthFieldMismatch(void)
{
    printf("packet length field does not match actual buffer length:\n");

    uint8_t payload[] = {0x01, 0x02, 0x03};
    uint8_t buf[5 + sizeof(payload)];
    size_t len = buildValidPacket(buf, payload, (uint8_t)sizeof(payload));

    /* Claim a larger payload length than what the buffer actually contains,
     * without resizing the buffer or recomputing the CRC -- this must be
     * caught by the length-consistency check, independent of the CRC check. */
    buf[2] = (uint8_t)(sizeof(payload) + 1);

    ParsedExamplePkt_t parsed;
    PacketStatus_t status = parseExamplePkt(buf, len, &parsed);

    TEST_ASSERT(status == PACKET_STATUS_INVALID_LENGTH,
                "mismatch between the embedded length byte and actual packetLen is rejected");
}

static void testInvalidCrc(void)
{
    printf("packet with corrupted CRC:\n");

    uint8_t payload[] = {0x10, 0x20, 0x30};
    uint8_t buf[5 + sizeof(payload)];
    size_t len = buildValidPacket(buf, payload, (uint8_t)sizeof(payload));

    /* Flip a bit in the payload after the CRC has already been computed and
     * embedded, so the CRC no longer matches the (now-different) contents. */
    buf[3] ^= 0x01;

    ParsedExamplePkt_t parsed;
    PacketStatus_t status = parseExamplePkt(buf, len, &parsed);

    TEST_ASSERT(status == PACKET_STATUS_INVALID_CRC, "packet with mismatched CRC is rejected");
}

static void testByteSwappedHeaderRejected(void)
{
    printf("negative: byte-swapped header bytes are rejected:\n");

    /* Deliberately writes the two header bytes in the wrong order -- this is
     * the classic endianness-confusion bug: 0xAA55 stored as {0x55, 0xAA}
     * instead of the correct {0xAA, 0x55}. Must be rejected as an invalid
     * header, not accidentally accepted. */
    uint8_t payload[] = {0x01};
    uint8_t buf[5 + sizeof(payload)];
    size_t len = buildValidPacket(buf, payload, (uint8_t)sizeof(payload));

    uint8_t tmp = buf[0];
    buf[0] = buf[1];
    buf[1] = tmp;

    ParsedExamplePkt_t parsed;
    PacketStatus_t status = parseExamplePkt(buf, len, &parsed);

    TEST_ASSERT(status == PACKET_STATUS_INVALID_HEADER, "byte-swapped header (endianness mix-up) is rejected");
}

static void testCrcFieldItselfCorrupted(void)
{
    printf("negative: corrupting only the trailing CRC field (payload untouched) is rejected:\n");

    /* Distinguishes "payload changed, so its true CRC no longer matches the
     * embedded one" (already covered by testInvalidCrc) from "payload is
     * completely correct, but the CRC bytes on the wire were corrupted in
     * transit" -- both must be rejected the same way. */
    uint8_t payload[] = {0xAB, 0xCD};
    uint8_t buf[5 + sizeof(payload)];
    size_t len = buildValidPacket(buf, payload, (uint8_t)sizeof(payload));

    buf[len - 1] ^= 0xFF; /* flip the low CRC byte only */

    ParsedExamplePkt_t parsed;
    PacketStatus_t status = parseExamplePkt(buf, len, &parsed);

    TEST_ASSERT(status == PACKET_STATUS_INVALID_CRC, "corrupted trailing CRC byte is rejected even with a valid payload");
}

static void testZeroLengthBuffer(void)
{
    printf("negative: zero-length packet buffer is rejected:\n");

    ParsedExamplePkt_t parsed;
    PacketStatus_t status = parseExamplePkt((const uint8_t *)"", 0, &parsed);

    TEST_ASSERT(status == PACKET_STATUS_INVALID_LENGTH, "a zero-length packet is rejected as invalid length");
}

static void testNullPacketPointer(void)
{
    printf("NULL packet pointer:\n");

    ParsedExamplePkt_t parsed;
    /* packetLen must be >= 5 here so the NULL check is what actually triggers
     * the rejection, rather than the earlier length check. */
    PacketStatus_t status = parseExamplePkt(NULL, 5, &parsed);

    TEST_ASSERT(status == PACKET_STATUS_INVALID_LENGTH, "NULL packet pointer is rejected rather than dereferenced");
}

static void testNullParsedPktPointer(void)
{
    printf("NULL parsedPkt pointer:\n");

    uint8_t payload[] = {0x01};
    uint8_t buf[5 + sizeof(payload)];
    size_t len = buildValidPacket(buf, payload, (uint8_t)sizeof(payload));

    PacketStatus_t status = parseExamplePkt(buf, len, NULL);

    TEST_ASSERT(status == PACKET_STATUS_INVALID_LENGTH, "NULL parsedPkt pointer is rejected rather than dereferenced");
}

static void testMaxPayloadLength(void)
{
    printf("maximum payload length (payLoadLen == 255):\n");

    uint8_t payload[255];
    memset(payload, 0x42, sizeof(payload));

    uint8_t buf[5 + sizeof(payload)];
    size_t len = buildValidPacket(buf, payload, (uint8_t)sizeof(payload));

    ParsedExamplePkt_t parsed;
    PacketStatus_t status = parseExamplePkt(buf, len, &parsed);

    TEST_ASSERT(status == PACKET_STATUS_OK, "packet with maximum (255-byte) payload parses OK");
    TEST_ASSERT(parsed.payLoadLen == 255, "parsed payLoadLen correctly reports 255");
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

    printf("\n%d / %d tests passed.\n", passedTests, totalTests);

    return (passedTests == totalTests) ? 0 : 1;
}

/**************************************** END OF packetParserTest.c ****************************************/