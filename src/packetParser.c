/******************************************************************************
 * @file packetParser.c
 * @brief This file contains the implementation of the packet parser functions.
 *
 * @author Abhishek
 *
 * @date July 2026
 ******************************************************************************/

#include <packetParser.h>

/**
 * @brief Parse an example packet.
 *
 * @details This function parses an example packet, extracting the payload length,
 *         payload data, and validating the CRC checksum. It checks for valid packet
 *         structure and integrity.
 *
 * @param packet Pointer to the packet data.
 * @param packetLen Length of the packet data.
 * @param parsedPkt Pointer to the structure to store the parsed packet information.
 *
 * @exception None
 *
 * @return PacketStatus_t Status of the parsing operation.
 */
PacketStatus_t parseExamplePkt(const uint8_t *packet, size_t packetLen, ParsedExamplePkt_t *parsedPkt)
{
    if (packetLen < 5) // Minimum length: 2 bytes header + 1 byte length + 2 byte CRC
    {
        return PACKET_STATUS_INVALID_LENGTH;
    }

    if (packet == NULL || parsedPkt == NULL)
    {
        return PACKET_STATUS_INVALID_LENGTH;
    }

    uint16_t header = ((uint16_t)packet[0] << 8) | packet[1];

    if (header != SIMPLEPKT_HDR)
    {
        return PACKET_STATUS_INVALID_HEADER;
    }

    uint8_t payLoadLen = packet[2];

    if (packetLen != (size_t)(5U + payLoadLen))
    {
        return PACKET_STATUS_INVALID_LENGTH;
    }

    const uint8_t *payLoad = &packet[3];
    uint16_t receivedCrc = ((uint16_t)packet[3 + payLoadLen] << 8) | packet[4 + payLoadLen];
    uint16_t calculatedCrc = 0;
    CRC16_Status_t crcSts = crc16Compute(packet, 3 + payLoadLen, &calculatedCrc);
    if (CRC16_SUCCESS == crcSts)
    {
        if (calculatedCrc != receivedCrc)
        {
            return PACKET_STATUS_INVALID_CRC;
        }
    }

    parsedPkt->payLoadLen = payLoadLen;
    parsedPkt->payLoad = payLoad;
    parsedPkt->isCkSumValid = true;
    return PACKET_STATUS_OK;
}

/****************************************END OF packetParser.c****************************************/