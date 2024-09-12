/**
 * File: httpd.h
 * Author: Diego Parrilla Santamaría
 * Date: August 2023
 * Copyright: 2024 - GOODDATA LABS SL
 * Description: Header file for the generic httpd server.
 */

#ifndef HTTPD_H
#define HTTPD_H

#include "debug.h"
#include "constants.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "lwip/apps/httpd.h"

// Function Prototypes
void httpd_server_init(const char *ssi_tags[], size_t num_tags, tSSIHandler ssi_handler_func, const tCGI *cgi_handlers, size_t num_cgi_handlers);

#endif // HTTPD_H
