/******************************************************************************
 * @file    packetParser.c
 * @brief   Implementation of the generic fixed-format packet parser.
 *
 * @author  Abhishek Doddagoudar
 * @date    July 2026
 *
 * @details See packetParser.h for the wire format, contract, and error code
 *          semantics. Field offsets and sizes come from protocol.h, the
 *          single shared definition of the on-wire layout.
 ******************************************************************************/

#include "packetParser.h"

#include "crc16.h"
#include "protocol.h"

/** Byte offsets into a well-formed packet buffer. */
#define PACKET_OFFSET_HEADER1 (0U)
#define PACKET_OFFSET_HEADER2 (1U)
#define PACKET_OFFSET_LENGTH (PACKET_HEADER_SIZE)
#define PACKET_OFFSET_COMMAND (PACKET_HEADER_SIZE + PACKET_LENGTH_SIZE)
#define PACKET_OFFSET_PAYLOAD (PACKET_HEADER_SIZE + PACKET_LENGTH_SIZE + PACKET_COMMAND_SIZE)

/**
 * @brief Assemble a big-endian 16-bit value from two bytes.
 *
 * @details Private helper -- replaces the prior bitUtils dependency. Only
 *          this module needs it, so it is static rather than shared.
 *
 * @param bytes Pointer to the first (most significant) of the two bytes.
 *
 * @return uint16_t The assembled value.
 */
static uint16_t readUint16BE(const uint8_t *bytes)
{
    return (uint16_t)(((uint16_t)bytes[0] << 8) | (uint16_t)bytes[1]);
}

/**
 * @brief Validate and decode a single packet from a raw byte buffer.
 *
 * @details Validates, in order: NULL pointers, minimum buffer size, header
 *          bytes, the decoded Length field's range, that rawLength actually
 *          contains the full declared packet, and finally the CRC-16 over
 *          Length + Command + Payload. parsedPacket is only written to on
 *          PACKET_PARSER_SUCCESS; on any error it is left untouched. Bytes
 *          in rawData beyond the declared packet size are not inspected.
 *
 * @param rawData      Pointer to the raw packet buffer.
 * @param rawLength    Number of bytes available at rawData (may exceed the
 *                      size of a single packet; see packetParser.h).
 * @param parsedPacket Pointer to store the decoded packet contents.
 *
 * @return PacketParserStatus_t Status of the operation.
 *
 * @retval PACKET_PARSER_SUCCESS Packet parsed and validated successfully.
 * @retval PACKET_PARSER_ERROR_NULL_POINTER rawData or parsedPacket is NULL.
 * @retval PACKET_PARSER_ERROR_INVALID_LENGTH rawLength is too small for even
 *         an empty-payload packet, the decoded Length field is out of range
 *         (0, or implying a payload > 255 bytes), or rawLength does not
 *         contain the full packet implied by the decoded Length field.
 * @retval PACKET_PARSER_ERROR_INVALID_HEADER Header1/Header2 do not match
 *         PACKET_HEADER1/PACKET_HEADER2.
 * @retval PACKET_PARSER_ERROR_CRC_MISMATCH The computed CRC-16 does not match
 *         the CRC field in the packet.
 */
PacketParserStatus_t packetParserParse(const uint8_t *rawData, size_t rawLength, ParsedPacket_t *parsedPacket)
{
    if ((rawData == NULL) || (parsedPacket == NULL))
    {
        return PACKET_PARSER_ERROR_NULL_POINTER;
    }

    if (rawLength < PACKET_MIN_PACKET_SIZE)
    {
        return PACKET_PARSER_ERROR_INVALID_LENGTH;
    }

    if ((rawData[PACKET_OFFSET_HEADER1] != PACKET_HEADER1) || (rawData[PACKET_OFFSET_HEADER2] != PACKET_HEADER2))
    {
        return PACKET_PARSER_ERROR_INVALID_HEADER;
    }

    uint16_t declaredLength = readUint16BE(&rawData[PACKET_OFFSET_LENGTH]);

    /* declaredLength covers Command (1 byte) + Payload (0-255 bytes), so its
     * valid range is [PACKET_COMMAND_SIZE, PACKET_COMMAND_SIZE + PACKET_MAX_PAYLOAD_SIZE]. */
    if ((declaredLength < PACKET_COMMAND_SIZE) ||
        (declaredLength > (PACKET_COMMAND_SIZE + PACKET_MAX_PAYLOAD_SIZE)))
    {
        return PACKET_PARSER_ERROR_INVALID_LENGTH;
    }

    size_t totalPacketSize = (size_t)PACKET_HEADER_SIZE + PACKET_LENGTH_SIZE + declaredLength + PACKET_CRC_SIZE;

    if (rawLength < totalPacketSize)
    {
        return PACKET_PARSER_ERROR_INVALID_LENGTH;
    }

    /* CRC covers Length + Command + Payload; header bytes are excluded. */
    const uint8_t *crcRegionStart = &rawData[PACKET_OFFSET_LENGTH];
    size_t crcRegionSize = (size_t)PACKET_LENGTH_SIZE + declaredLength;
    uint16_t computedCrc = 0;
    CRC16_Status_t crcSts = crc16Compute(crcRegionStart, crcRegionSize, &computedCrc);

    if(crcSts != CRC16_SUCCESS)
    {
        return CRC16_ERROR_MISMATCH;
    }

    size_t crcOffset = (size_t)PACKET_OFFSET_LENGTH + crcRegionSize;
    uint16_t receivedCrc = readUint16BE(&rawData[crcOffset]);

    if (computedCrc != receivedCrc)
    {
        return PACKET_PARSER_ERROR_CRC_MISMATCH;
    }

    uint16_t payloadLength = (uint16_t)(declaredLength - PACKET_COMMAND_SIZE);

    parsedPacket->commandId = rawData[PACKET_OFFSET_COMMAND];
    parsedPacket->payloadLength = payloadLength;
    parsedPacket->payload = (payloadLength > 0U) ? &rawData[PACKET_OFFSET_PAYLOAD] : NULL;

    return PACKET_PARSER_SUCCESS;
}

/**
 * @brief Peek the total on-wire size of the packet starting at rawData.
 *
 * @details Reads only the Length field (at a fixed offset past the two
 *          header bytes) and returns header + length-field + declared
 *          Length + CRC. Intended for framing code that needs to know how
 *          many bytes to buffer before calling packetParserParse(); it does
 *          NOT validate the header, the length range, or the CRC, and does
 *          NOT check that rawData actually contains that many bytes -- the
 *          caller must ensure at least PACKET_HEADER_SIZE + PACKET_LENGTH_SIZE
 *          bytes are available at rawData before calling this.
 *
 * @param rawData Pointer to the raw packet buffer.
 *
 * @return size_t Total on-wire packet size in bytes implied by the Length
 *         field, or 0 if rawData is NULL.
 */
size_t packetParserGetPacketSize(const uint8_t *rawData)
{
    if (rawData == NULL)
    {
        return 0U;
    }

    uint16_t declaredLength = readUint16BE(&rawData[PACKET_OFFSET_LENGTH]);

    return (size_t)PACKET_HEADER_SIZE + PACKET_LENGTH_SIZE + declaredLength + PACKET_CRC_SIZE;
}

/******************************* END OF FILE **********************************/