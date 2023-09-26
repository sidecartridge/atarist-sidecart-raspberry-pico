/**
 * File: tprotocol.c
 * Author: Diego Parrilla Santamaría
 * Date: August 2023
 * Copyright: 2023 - GOODDATA LABS SL
 * Description: Parse the protocol used to communicate with the ROM3
 */

#include "include/tprotocol.h"

static uint64_t last_header_found = 0;

// Global variable to keep track of current parsing state
static TPParseStep nextTPstep = HEADER_DETECTION;

// Placeholder structure for parsed data
static TransmissionProtocol transmission;

// Placeholder functions for each step
static void __not_in_flash_func(detect_header)(uint16_t data)
{
    if (data == PROTOCOL_HEADER)
    {
        nextTPstep = COMMAND_READ;
    }
}

static void __not_in_flash_func(read_command)(uint16_t data)
{
    transmission.command_id = data;
    nextTPstep = PAYLOAD_SIZE_READ;
}

static void __not_in_flash_func(read_payload_size)(uint16_t data)
{
    if (data > 0)
    {
        transmission.payload_size = data; // Store incoming data as payload size
        // transmission.payload = malloc(transmission.payload_size * sizeof(unsigned char)); // Allocate memory for the payload
        // if (!transmission.payload)
        // { // Ensure memory was allocated successfully
        //     printf("Error: Could not allocate memory for payload!\n");
        //     exit(1); // Exit or handle the error appropriately
        // }
        //
        transmission.bytes_read = 0;
        nextTPstep = PAYLOAD_READ_START;
    }
    else
    {
        transmission.bytes_read = 0;
        nextTPstep = PAYLOAD_READ_END;
    }
}

static void __not_in_flash_func(read_payload)(uint16_t data)
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

void __not_in_flash_func(process_command)(ProtocolCallback callback)
{
    printf("COMMAND: %d / ", transmission.command_id);
    printf("PAYLOAD SIZE: %d / ", transmission.payload_size);
    printf("PAYLOAD: ");
    for (int i = 0; i < transmission.payload_size; i += 2)
    {
        uint16_t value = *((uint16_t *)(&transmission.payload[i]));
        printf("0x%04X ", value);
    }
    printf("\n");

    // Here should pass the transmission message to a function that will handle the different commands
    // I think a good aproach would be to have a callback to custom functions that will handle the different commands

    if (callback)
    {
        callback(&transmission);
    }

    transmission.command_id = 0;
    transmission.payload_size = 0;
    transmission.bytes_read = 0;
    // Handle the end of the payload reading. Maybe reset state or take some action based on received data.
    nextTPstep = HEADER_DETECTION; // Resetting to start for the next message.
}

void __not_in_flash_func(parse_protocol)(uint16_t data, ProtocolCallback callback)
{
    uint64_t new_header_found = time_us_64();
    if (new_header_found - last_header_found > PROTOCOL_READ_RESTART_MICROSECONDS)
    {
        nextTPstep = HEADER_DETECTION;
        //        printf("Restarting protocol read. Lapse. %i\n", new_header_found - last_header_found);
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
