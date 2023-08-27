/**
 * File: tprotocol.h
 * Author: Diego Parrilla Santamaría
 * Date: August 2023
 * Copyright: 2023 - GOODDATA LABS SL
 * Description: Header for the protocol parser for ROM3
 */

#ifndef TPROTOCOL_H
#define TPROTOCOL_H

#include "constants.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROTOCOL_HEADER 0xABCD
#define PROTOCOL_READ_RESTART_MICROSECONDS 10000

typedef enum
{
    HEADER_DETECTION,
    COMMAND_READ,
    PAYLOAD_SIZE_READ,
    PAYLOAD_READ_START,
    PAYLOAD_READ_INPROGRESS,
    PAYLOAD_READ_END
} TPParseStep;

typedef struct
{
    uint16_t command_id;    // Command ID
    uint16_t payload_size;  // Size of the payload
    unsigned char *payload; // Pointer to the payload data
    uint16_t bytes_read;    // To keep track of how many bytes of the payload we've read so far.
} TransmissionProtocol;

typedef void (*ProtocolCallback)(const TransmissionProtocol *);

// Function to parse the protocol
void parse_protocol(uint16_t data, ProtocolCallback callback);

#endif // TPROTOCOL_H
