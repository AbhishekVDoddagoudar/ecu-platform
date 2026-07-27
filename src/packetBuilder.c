/******************************************************************************
 * @file    packetBuilder.c
 * @brief   Implementation of the fixed-format packet builder.
 *
 * @author  Abhishek Doddagoudar
 * @date    July 2026
 *
 * @details See packetBuilder.h for the wire format, contract, and error code
 *          semantics. Field offsets and sizes come from protocol.h, the
 *          single shared definition of the on-wire layout also used by
 *          packetParser.c.
 ******************************************************************************/


#include <string.h>

#include <packetBuilder.h>
#include <crc16.h>
#include <protocol.h>

/** Byte offsets into the packet buffer being built. */
#define PACKET_OFFSET_HEADER1 (0U)
#define PACKET_OFFSET_HEADER2 (1U)
#define PACKET_OFFSET_LENGTH (PACKET_HEADER_SIZE)
#define PACKET_OFFSET_COMMAND (PACKET_HEADER_SIZE + PACKET_LENGTH_SIZE)
#define PACKET_OFFSET_PAYLOAD (PACKET_HEADER_SIZE + PACKET_LENGTH_SIZE + PACKET_COMMAND_SIZE)

/**
 * @internal
 * @brief Write a 16-bit value into two bytes, big-endian.
 *
 * @details Private helper, mirroring packetParser.c's readUint16BE(). Kept
 *          local and static rather than shared: it is three lines, and a
 *          shared bitUtils-style dependency was deliberately removed from
 *          this project in Sprint 5.
 *
 * @param bytes Pointer to the destination for the two bytes (most
 *              significant byte first).
 * @param value Value to write.
 */
static void writeUint16BE(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)(value >> 8);
    bytes[1] = (uint8_t)(value & 0xFFU);
}

/**
 * @brief Build a complete, CRC-checked packet into a caller-owned buffer.
 *
 * @details Validates, in order: NULL pointers (packetBuffer, packetLength,
 *          and payload when payloadLength > 0), that payloadLength is within
 *          [0, PACKET_MAX_PAYLOAD_SIZE], and that packetBufferSize is large
 *          enough for the resulting packet. Nothing is written to
 *          packetBuffer or packetLength unless the build succeeds. On
 *          success, packetBuffer holds a complete on-wire frame (header,
 *          length, command, payload, CRC) and *packetLength is set to its
 *          total size.
 *
 * @param commandId        Command byte to embed.
 * @param payload          Payload bytes to embed. May be NULL only if
 *                         payloadLength is 0.
 * @param payloadLength    Number of payload bytes (0-255).
 * @param packetBuffer     Caller-owned destination buffer for the built packet.
 * @param packetBufferSize Number of bytes available at packetBuffer.
 * @param packetLength     Pointer to store the total number of bytes written.
 *
 * @return PacketBuilderStatus_t Status of the operation.
 *
 * @retval PACKET_BUILDER_SUCCESS Packet built successfully.
 * @retval PACKET_BUILDER_ERROR_NULL_POINTER packetBuffer or packetLength is
 *         NULL, or payload is NULL while payloadLength is greater than 0.
 * @retval PACKET_BUILDER_ERROR_INVALID_LENGTH payloadLength exceeds
 *         PACKET_MAX_PAYLOAD_SIZE.
 * @retval PACKET_BUILDER_ERROR_BUFFER_TOO_SMALL packetBufferSize is smaller
 *         than the total size the resulting packet requires.
 */
PacketBuilderStatus_t packetBuilderBuildPacket(uint8_t commandId,
                                               const uint8_t *payload,
                                               uint16_t payloadLength,
                                               uint8_t *packetBuffer,
                                               size_t packetBufferSize,
                                               size_t *packetLength)
{
    if ((packetBuffer == NULL) || (packetLength == NULL))
    {
        return PACKET_BUILDER_ERROR_NULL_POINTER;
    }

    if ((payload == NULL) && (payloadLength > 0U))
    {
        return PACKET_BUILDER_ERROR_NULL_POINTER;
    }

    if (payloadLength > PACKET_MAX_PAYLOAD_SIZE)
    {
        return PACKET_BUILDER_ERROR_INVALID_LENGTH;
    }

    size_t totalPacketSize = (size_t)PACKET_MIN_PACKET_SIZE + payloadLength;

    if (packetBufferSize < totalPacketSize)
    {
        return PACKET_BUILDER_ERROR_BUFFER_TOO_SMALL;
    }

    packetBuffer[PACKET_OFFSET_HEADER1] = PACKET_HEADER1;
    packetBuffer[PACKET_OFFSET_HEADER2] = PACKET_HEADER2;

    uint16_t declaredLength = (uint16_t)(PACKET_COMMAND_SIZE + payloadLength);
    writeUint16BE(&packetBuffer[PACKET_OFFSET_LENGTH], declaredLength);

    packetBuffer[PACKET_OFFSET_COMMAND] = commandId;

    if (payloadLength > 0U)
    {
        memcpy(&packetBuffer[PACKET_OFFSET_PAYLOAD], payload, payloadLength);
    }

    /* CRC covers Length + Command + Payload; header bytes are excluded. */
    const uint8_t *crcRegionStart = &packetBuffer[PACKET_OFFSET_LENGTH];
    size_t crcRegionSize = (size_t)PACKET_LENGTH_SIZE + declaredLength;
    uint16_t crc = 0;
    CRC16_Status_t crcSts = crc16Compute(crcRegionStart, crcRegionSize,&crc);

    if(crcSts != CRC16_SUCCESS)
    {
        return CRC16_ERROR_MISMATCH;
    }

    size_t crcOffset = (size_t)PACKET_OFFSET_LENGTH + crcRegionSize;
    writeUint16BE(&packetBuffer[crcOffset], crc);

    *packetLength = totalPacketSize;

    return PACKET_BUILDER_SUCCESS;
}

/******************************* END OF FILE **********************************/