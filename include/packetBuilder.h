/******************************************************************************
 * @file    packetBuilder.h
 * @brief   Public interface for building fixed-format packets (see protocol.h).
 *
 * @author  Abhishek Doddagoudar
 * @date    July 2026
 *
 * @details
 * Constructs packets of the form defined in protocol.h -- the inverse
 * operation of packetParser.h:
 *
 *      +---------+---------+----------+---------+-----------+---------+
 *      | Header1 | Header2 | Length   | Command |  Payload  |  CRC16  |
 *      | 0xAA    | 0x55    | 2B (BE)  | 1B      | 0-255 B   | 2B (BE) |
 *      +---------+---------+----------+---------+-----------+---------+
 *
 * Length covers Command + Payload only. CRC16 (CRC-16-CCITT-FALSE) is
 * computed over Length + Command + Payload; header bytes are excluded.
 *
 * This module is transport-agnostic: it produces a byte array and a length
 * and knows nothing about UART, CAN, Ethernet, or any other transport --
 * that is a concern for a layer above this one.
 *
 * Dependencies:
 *  - protocol.h (shared wire-format constants; see that header)
 *  - crc16      (CRC-16-CCITT-FALSE computation)
 *
 * Thread Safety:
 *  - Reentrant. No internal or global state; all state lives in the
 *    caller-owned output buffer.
 *
 * Dynamic Memory:
 *  - None. The caller supplies the output buffer; this module never
 *    allocates.
 *
 * Example Usage:
 *
 *      uint8_t payload[] = {0x11, 0x22, 0x33, 0x44};
 *      uint8_t packet[PACKET_MAX_PACKET_SIZE];
 *      size_t packetLength;
 *
 *      PacketBuilderStatus_t status = packetBuilderBuildPacket(
 *          0x10, payload, sizeof(payload), packet, sizeof(packet), &packetLength);
 *
 *      if (status == PACKET_BUILDER_SUCCESS)
 *      {
 *          // packet[0..packetLength-1] is a complete, ready-to-send frame.
 *      }
 *
 ******************************************************************************/

#ifndef PACKET_BUILDER_H
#define PACKET_BUILDER_H

#include <stddef.h>
#include <stdint.h>

#include "protocol.h"

/**
 * @brief Packet builder status codes.
 */
typedef enum
{
    PACKET_BUILDER_SUCCESS = 0,
    PACKET_BUILDER_ERROR_NULL_POINTER,
    PACKET_BUILDER_ERROR_INVALID_LENGTH,
    PACKET_BUILDER_ERROR_BUFFER_TOO_SMALL
} PacketBuilderStatus_t;

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
 * @param commandId       Command byte to embed.
 * @param payload         Payload bytes to embed. May be NULL only if
 *                         payloadLength is 0.
 * @param payloadLength    Number of payload bytes (0-255).
 * @param packetBuffer    Caller-owned destination buffer for the built packet.
 * @param packetBufferSize Number of bytes available at packetBuffer.
 * @param packetLength    Pointer to store the total number of bytes written.
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
                                               size_t *packetLength);

#endif /* PACKET_BUILDER_H */

/******************************* END OF FILE **********************************/