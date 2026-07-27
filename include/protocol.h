/******************************************************************************
 * @file    protocol.h
 * @brief   Shared wire-format constants for the fixed-format packet protocol.
 *
 * @author  Abhishek Doddagoudra
 * @date    July 2026
 *
 * @details
 * Single source of truth for the on-wire packet layout:
 *
 *      +---------+---------+----------+---------+-----------+---------+
 *      | Header1 | Header2 | Length   | Command |  Payload  |  CRC16  |
 *      | 0xAA    | 0x55    | 2B (BE)  | 1B      | 0-255 B   | 2B (BE) |
 *      +---------+---------+----------+---------+-----------+---------+
 *
 * Any module that speaks this protocol -- packetParser, packetBuilder,
 * transport, tests -- includes this header instead of defining its own
 * copy of these constants. The parser is one consumer of the protocol
 * definition, not its owner.
 *
 ******************************************************************************/

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

/** Header sentinel values. */
#define PACKET_HEADER1 (0xAAU)
#define PACKET_HEADER2 (0x55U)

/** Field sizes on the wire, in bytes. */
#define PACKET_HEADER_SIZE (2U)
#define PACKET_LENGTH_SIZE (2U)
#define PACKET_COMMAND_SIZE (1U)
#define PACKET_CRC_SIZE (2U)

/** Maximum supported payload size in bytes. */
#define PACKET_MAX_PAYLOAD_SIZE (255U)

/**
 * Smallest valid on-wire packet size (empty payload):
 * header + length + command + crc.
 */
#define PACKET_MIN_PACKET_SIZE \
    (PACKET_HEADER_SIZE + PACKET_LENGTH_SIZE + PACKET_COMMAND_SIZE + PACKET_CRC_SIZE)

/** Largest valid on-wire packet size (maximum payload). */
#define PACKET_MAX_PACKET_SIZE (PACKET_MIN_PACKET_SIZE + PACKET_MAX_PAYLOAD_SIZE)

#endif /* PROTOCOL_H */

/******************************* END OF FILE **********************************/