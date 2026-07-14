/******************************************************************************
 * @file   packetParser.h
 * @brief  Public interface for packet parsing utilities.
 *
 * @author Abhishek Doddagoudar
 *
 * @date   July 2026
 ******************************************************************************/

#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <crc16.h>


#define SIMPLEPKT_HDR 0xAA55 /**< Header for SimplePkt */

/**
 * @brief Enumerates the possible statuses of a parsed packet.
 *
 */
typedef enum
{
    PACKET_STATUS_OK = 0,           /**< Packet parsed successfully */
    PACKET_STATUS_INVALID_LENGTH,   /**< Invalid packet length */
    PACKET_STATUS_INVALID_HEADER,   /**< Invalid packet header */
    PACKET_STATUS_INVALID_CRC,      /**< Invalid CRC checksum */
    PACKET_STATUS_INCOMPLETE_PACKET /**< Incomplete packet received */
} PacketStatus_t;

typedef struct
{
    uint16_t header;
    uint8_t payLoadLen;
    const uint8_t *payLoad;
    uint16_t crc;
} SimplePacketFrame_t;

/**
 * @brief Structure to hold the parsed packet data.
 *
 */
typedef struct
{
    uint8_t payLoadLen;
    const uint8_t *payLoad;
    bool isCkSumValid;
} ParsedExamplePkt_t;

/**
 * @brief Parses a SimplePkt packet.
 *
 * @param packet Pointer to the input packet data
 * @param packetLen Length of the input packet data
 * @param parsedPkt Pointer to the structure where parsed packet data will be stored
 *
 * @return PacketStatus_t Status of the packet parsing operation
 */
PacketStatus_t parseExamplePkt(const uint8_t *packet, size_t packetLen, ParsedExamplePkt_t *parsedPkt);

/********************************************END OF packetParser.h**********************************/