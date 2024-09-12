/**
 * File: httpd.c
 * Author: Diego Parrilla Santamaría
 * Date: August 2024
 * Copyright: 2024 - GOODDATA LABS SL
 * Description: HTTPD server functions
 */

#include "include/httpd.h"

/**
 * @brief Initializes the HTTP server with optional SSI tags, CGI handlers, and an SSI handler function.
 *
 * This function initializes the HTTP server and sets up the provided Server Side Include (SSI) tags,
 * Common Gateway Interface (CGI) handlers, and SSI handler function. It first calls the httpd_init()
 * function to initialize the HTTP server.
 *
 * The filesystem for the HTTP server is in the 'fs' directory in the project root.
 *
 * @param ssi_tags An array of strings representing the SSI tags to be used in the server-side includes.
 * @param num_tags The number of SSI tags in the ssi_tags array.
 * @param ssi_handler_func A pointer to the function that handles SSI tags.
 * @param cgi_handlers An array of tCGI structures representing the CGI handlers to be used.
 * @param num_cgi_handlers The number of CGI handlers in the cgi_handlers array.
 */
void httpd_server_init(const char *ssi_tags[], size_t num_tags, tSSIHandler ssi_handler_func, const tCGI *cgi_handlers, size_t num_cgi_handlers)
{
    httpd_init();

    // SSI Initialization
    if (num_tags > 0)
    {
        for (size_t i = 0; i < num_tags; i++)
        {
            LWIP_ASSERT("tag too long for LWIP_HTTPD_MAX_TAG_NAME_LEN",
                        strlen(ssi_tags[i]) <= LWIP_HTTPD_MAX_TAG_NAME_LEN);
        }
        http_set_ssi_handler(ssi_handler_func, ssi_tags, num_tags);
    }
    else
    {
        DPRINTF("No SSI tags defined.\n");
    }

    // CGI Initialization
    if (num_cgi_handlers > 0)
    {
        http_set_cgi_handlers(cgi_handlers, num_cgi_handlers);
    }
    else
    {
        DPRINTF("No CGI handlers defined.\n");
    }

    DPRINTF("HTTP server initialized.\n");
}

// The main function should be as follows:
// int main(void)
// {
//     // Initialize the HTTP server with SSI tags and CGI handlers
//     httpd_server_init(ssi_tags, LWIP_ARRAYSIZE(ssi_tags), cgi_handlers, LWIP_ARRAYSIZE(cgi_handlers));
//     // Other application initialization code goes here
//     // ...
//     // Enter the main loop or task processing loop
//     while (1)
//     {
//         tight_loop_contents();
//         if (network_ready)
//         {
// #if PICO_CYW43_ARCH_POLL
//             network_poll();
// #endif
//             cyw43_arch_lwip_begin();
//             cyw43_arch_lwip_check();
//             cyw43_arch_lwip_end();
//         }
//     }
// }