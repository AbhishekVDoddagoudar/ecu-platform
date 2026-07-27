/******************************************************************************
 * @file    packetParser.h
 * @brief   Public interface for a generic fixed-format packet parser.
 *
 * @author  Abhishek Doddagoudar
 * @date    July 2026
 *
 * @details
 * Validates and decodes packets of the form defined in protocol.h:
 *
 *      +---------+---------+----------+---------+-----------+---------+
 *      | Header1 | Header2 | Length   | Command |  Payload  |  CRC16  |
 *      | 0xAA    | 0x55    | 2B (BE)  | 1B      | 0-255 B   | 2B (BE) |
 *      +---------+---------+----------+---------+-----------+---------+
 *
 * Length covers Command + Payload only (not the header bytes, not itself,
 * not the CRC). CRC16 (CRC-16-CCITT-FALSE) is computed over
 * Length + Command + Payload; header bytes are excluded from the CRC.
 *
 * rawLength means "at least this many valid bytes are available," not
 * "exactly one packet is present." Bytes beyond the declared packet size
 * are not inspected and do not cause an error; detecting or splitting
 * multiple concatenated packets in one buffer is a transport/framing
 * concern above this module.
 *
 * See docs/packetParser-requirements.md and docs/packetParser-design.md
 * for the full requirements analysis and design rationale, including why
 * the payload is zero-copy rather than copied (Memory Ownership Decision).
 *
 * Features:
 *  - Header, length, and CRC validation
 *  - Zero-copy payload access (no dynamic allocation)
 *  - Reentrant, platform-independent (explicit big-endian wire format)
 *
 * Thread Safety:
 *  - Reentrant. No internal or global state; all state lives in the
 *    caller-owned ParsedPacket_t output and the caller's own raw buffer.
 *
 * Dynamic Memory:
 *  - None. The parsed payload is a pointer into the caller's raw buffer,
 *    not a copy. That pointer is valid only as long as the caller's raw
 *    buffer remains valid and unmodified. Callers whose receive buffer may
 *    be overwritten before the parsed result is consumed (e.g. DMA/ISR
 *    driven reception into a reused buffer) are responsible for copying
 *    the buffer themselves before calling packetParserParse().
 *
 * Dependencies:
 *  - protocol.h (shared wire-format constants; see that header)
 *  - crc16      (CRC-16-CCITT-FALSE verification)
 *
 * Example Usage:
 *
 *      ParsedPacket_t packet;
 *      PacketParserStatus_t status = packetParserParse(rawBuffer, rawLength, &packet);
 *
 *      if (status == PACKET_PARSER_SUCCESS)
 *      {
 *          // packet.commandId, packet.payloadLength, packet.payload are valid
 *          // for as long as rawBuffer itself remains valid and unmodified.
 *      }
 *
 ******************************************************************************/

#ifndef PACKET_PARSER_H
#define PACKET_PARSER_H

#include <stddef.h>
#include <stdint.h>

#include <protocol.h>

/**
 * @brief Decoded, validated packet contents.
 *
 * @details payload points into the caller's original raw buffer passed to
 *          packetParserParse() -- it is not a copy (see design doc, Memory
 *          Ownership Decision). It is valid only as long as that raw buffer
 *          remains valid and unmodified. If payloadLength is 0, payload is
 *          NULL.
 */
typedef struct
{
    uint8_t commandId;      /**< Decoded Command ID.                                    */
    uint16_t payloadLength; /**< Number of payload bytes (0-255).                        */
    const uint8_t *payload; /**< Pointer into the caller's raw buffer; NULL if length 0. */
} ParsedPacket_t;

/**
 * @brief Packet parser status codes.
 *
 * @details See design doc section "Error Codes" for how each first-principles
 *          failure identified during requirements analysis maps onto these
 *          four codes.
 */
typedef enum
{
    PACKET_PARSER_SUCCESS = 0,
    PACKET_PARSER_ERROR_NULL_POINTER,
    PACKET_PARSER_ERROR_INVALID_LENGTH,
    PACKET_PARSER_ERROR_INVALID_HEADER,
    PACKET_PARSER_ERROR_CRC_MISMATCH
} PacketParserStatus_t;

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
 *                      size of a single packet; see file-level comment).
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
PacketParserStatus_t packetParserParse(const uint8_t *rawData, size_t rawLength, ParsedPacket_t *parsedPacket);

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
size_t packetParserGetPacketSize(const uint8_t *rawData);

#endif /* PACKET_PARSER_H */

/******************************* END OF FILE **********************************/