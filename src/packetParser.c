/******************************************************************************
 * @file packetParser.c
 * @brief This file contains the implementation of the packet parser functions.
 *
 * @author Abhishek
 *
 * @date 2024-06-15
 ******************************************************************************/

#include <packetParser.h>

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
    uint16_t calculatedCrc = crc16Calculate(packet, 3 + payLoadLen);
    if (calculatedCrc != receivedCrc)
    {
        return PACKET_STATUS_INVALID_CRC;
    }

    parsedPkt->payLoadLen = payLoadLen;
    parsedPkt->payLoad = payLoad;
    parsedPkt->isCkSumValid = true;
    return PACKET_STATUS_OK;
}