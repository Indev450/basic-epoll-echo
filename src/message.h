#pragma once

#include <stddef.h>

typedef enum {
    RESPONSE_NONE,
    RESPONSE_UNKNOWN, // "unknown command"
    RESPONSE_MIRROR,
    RESPONSE_TIME,
    RESPONSE_STATS,
    RESPONSE_SHUTDOWN,
} response_type_t;

response_type_t parse_message(const char *message, size_t length);
