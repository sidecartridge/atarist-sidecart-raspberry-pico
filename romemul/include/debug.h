#ifndef DEBUG_H
#define DEBUG_H

/**
 * @brief A macro to print debug
 *
 * @param fmt The format string for the debug message, similar to printf.
 * @param ... Variadic arguments corresponding to the format specifiers in the fmt parameter.
 */
#if defined(_DEBUG) && (_DEBUG != 0)
#define DPRINTF(fmt, ...)                                                                  \
    do                                                                                     \
    {                                                                                      \
        const char *file = strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__; \
        fprintf(stderr, "%s:%d:%s(): " fmt "", file,                                       \
                __LINE__, __func__, ##__VA_ARGS__);                                        \
    } while (0)
#define DPRINTFRAW(fmt, ...)                 \
    do                                       \
    {                                        \
        fprintf(stderr, fmt, ##__VA_ARGS__); \
    } while (0)

#else
#define DPRINTF(fmt, ...)
#define DPRINTFRAW(fmt, ...)
#endif

#endif // DEBUG_H