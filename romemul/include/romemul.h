/**
 * File: romemul.h
 * Author: Diego Parrilla Santamaría
 * Date: July 2023
 * Copyright: 2023 - GOODDATA LABS SL
 * Description: Header file for the ROM emulator C program.
 */

#ifndef ROMEMUL_H
#define ROMEMUL_H

#include "constants.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/structs/bus_ctrl.h"
#include "pico/cyw43_arch.h"

#include "../../build/romemul.pio.h"

// Debug macros
#ifdef _DEBUG
#include <time.h>
#include <sys/time.h>
/**
 * @brief A macro to print debug information with timestamp.
 *
 * This macro is designed to print debug information with a timestamp.
 * It uses the gettimeofday function to get the current time with precision down to the microsecond.
 * The time is then formatted into a more human-readable timestamp.
 *
 * The debug message is printed either to stderr or to a file if the environment variable 'LOG_FILE' is set.
 * When writing to a file, it ensures the message is written immediately by calling fflush.
 *
 * @param fmt The format string for the debug message, similar to printf.
 * @param ... Variadic arguments corresponding to the format specifiers in the fmt parameter.
 */
#define DEBUG_PRINT(fmt, ...)                                                                              \
    do                                                                                                     \
    {                                                                                                      \
        struct timeval tv;                                                                                 \
        gettimeofday(&tv, NULL);                                                                           \
        time_t rawtime = tv.tv_sec;                                                                        \
        struct tm *ptm = localtime(&rawtime);                                                              \
        char buffDbg[25];                                                                                  \
        strftime(buffDbg, sizeof(buffDbg), "%d/%m/%y - %H:%M:%S", ptm);                                    \
        if (getenv("LOG_FILE") != NULL)                                                                    \
        {                                                                                                  \
            if (debug_file == NULL)                                                                        \
                debug_file = fopen(getenv("LOG_FILE"), "a");                                               \
            if (debug_file != NULL)                                                                        \
            {                                                                                              \
                fprintf(debug_file, "%s.%03ld - DEBUG - " fmt, buffDbg, tv.tv_usec / 1000, ##__VA_ARGS__); \
                fflush(debug_file);                                                                        \
            }                                                                                              \
        }                                                                                                  \
        else                                                                                               \
        {                                                                                                  \
            fprintf(stderr, "%s.%03ld - DEBUG - " fmt, buffDbg, tv.tv_usec / 1000, ##__VA_ARGS__);         \
        }                                                                                                  \
    } while (0)
/**
 * @brief Prints a 32-bit unsigned integer value in binary format.
 *
 * This macro takes a 32-bit unsigned integer value as input and prints it in binary format,
 * with bits being printed from MSB to LSB. Spaces are inserted every 8 bits for better readability.
 *
 * @param value The 32-bit unsigned integer value to print in binary format.
 */
#define PRINT_BINARY(value)                                \
    do                                                     \
    {                                                      \
        for (int bitIndex = 31; bitIndex >= 0; --bitIndex) \
        {                                                  \
            printf("%d", ((value) >> bitIndex) & 1);       \
            if (bitIndex % 8 == 0)                         \
            {                                              \
                printf(" ");                               \
            }                                              \
        }                                                  \
        printf("\n");                                      \
    } while (0)
#else
#define DEBUG_PRINT(fmt, ...)
#define PRINT_BINARY(value)
#endif

typedef void (*IRQInterceptionCallback)();

extern int read_addr_rom_dma_channel;
extern int lookup_data_rom_dma_channel;

// Function Prototypes
int init_romemul(IRQInterceptionCallback requestCallback, IRQInterceptionCallback responseCallback, bool copyFlashToRAM);

#endif // ROMEMUL_H
