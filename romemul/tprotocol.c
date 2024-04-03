/**
 * File: tprotocol.c
 * Author: Diego Parrilla Santamaría
 * Date: August 2023
 * Copyright: 2023 - GOODDATA LABS SL
 * Description: Parse the protocol used to communicate with the ROM3
 */

#include "include/tprotocol.h"

uint64_t last_header_found = 0;
uint64_t new_header_found = 0;

// Global variable to keep track of current parsing state
TPParseStep nextTPstep = HEADER_DETECTION;

// Placeholder structure for parsed data
TransmissionProtocol transmission;

// Placeholder functions for each step
inline static void __not_in_flash_func(detect_header)(uint16_t data)
{
    if (data == PROTOCOL_HEADER)
    {
        nextTPstep = COMMAND_READ;
    }
}

inline static void __not_in_flash_func(read_command)(uint16_t data)
{
    transmission.command_id = data;
    nextTPstep = PAYLOAD_SIZE_READ;
}

inline static void __not_in_flash_func(read_payload_size)(uint16_t data)
{
    if (data > 0)
    {
        transmission.payload_size = data; // Store incoming data as payload size
        nextTPstep = PAYLOAD_READ_START;
    }
    else
    {
        nextTPstep = PAYLOAD_READ_END;
    }
    transmission.bytes_read = 0;
}

inline static void __not_in_flash_func(read_payload)(uint16_t data)
{
    *((uint16_t *)&transmission.payload[transmission.bytes_read]) = data;
    transmission.bytes_read += 2;

    if (transmission.bytes_read >= transmission.payload_size)
    {
        nextTPstep = PAYLOAD_READ_END;
    }
    else
    {
        nextTPstep = PAYLOAD_READ_INPROGRESS;
    }
}

void init_protocol_parser()
{
    transmission.command_id = 0;
    transmission.payload_size = 0;
    transmission.payload = malloc(MAX_PROTOCOL_PAYLOAD_SIZE);
    transmission.bytes_read = 0;
}

void terminate_protocol_parser()
{
    if (transmission.payload)
    {
        free(transmission.payload);
        transmission.payload = NULL; // Set the pointer to NULL after freeing to avoid potential double freeing and other issues
    }
}

inline void __not_in_flash_func(process_command)(ProtocolCallback callback)
{
#if defined(_DEBUG) && (_DEBUG != 0) && defined(SHOW_COMMANDS) && (SHOW_COMMANDS != 0)
    DPRINTF("COMMAND: %d / PAYLOAD SIZE: %d / PAYLOAD: ", transmission.command_id, transmission.payload_size);
    for (int i = 0; i < transmission.payload_size; i += 2)
    {
        uint16_t value = *((uint16_t *)(&transmission.payload[i]));
        DPRINTFRAW("0x%04X ", value);
    }
    DPRINTFRAW("\n");
#endif
    // Here should pass the transmission message to a function that will handle the different commands
    // I think a good aproach would be to have a callback to custom functions that will handle the different commands

    if (callback)
    {
        callback(&transmission);
    }

    transmission.command_id = 0;
    transmission.payload_size = 0;
    transmission.bytes_read = 0;
    last_header_found = 0;
    // Handle the end of the payload reading. Maybe reset state or take some action based on received data.
    nextTPstep = HEADER_DETECTION; // Resetting to start for the next message.
}

inline void __not_in_flash_func(parse_protocol)(uint16_t data, ProtocolCallback callback)
{
    new_header_found = (((uint64_t)timer_hw->timerawh) << 32u | timer_hw->timerawl);
    if (new_header_found - last_header_found > PROTOCOL_READ_RESTART_MICROSECONDS)
    {
        nextTPstep = HEADER_DETECTION;
    }
    switch (nextTPstep)
    {
    case HEADER_DETECTION:
        detect_header(data);
        last_header_found = new_header_found;
        break;

    case COMMAND_READ:
        read_command(data);
        break;

    case PAYLOAD_SIZE_READ:
        read_payload_size(data);
        // If PAYLOAD_READ_END here, means we've finished reading the payload
        if (nextTPstep == PAYLOAD_READ_END)
        {
            process_command(callback);
        }
        break;

    case PAYLOAD_READ_START:
    case PAYLOAD_READ_INPROGRESS:
    case PAYLOAD_READ_END:
        if (transmission.bytes_read < transmission.payload_size)
        {
            read_payload(data);
        }
        // If PAYLOAD_READ_END here, means we've finished reading the payload
        if (nextTPstep == PAYLOAD_READ_END)
        {
            process_command(callback);
        }
        break;
    }
}
