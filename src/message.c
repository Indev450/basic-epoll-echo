#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "message.h"

response_type_t parse_message(const char *message, size_t length)
{
    if (length == 0)
        return RESPONSE_MIRROR;

    if (message[0] != '/')
        return RESPONSE_MIRROR;

    message++;
    length--;

    if (strncmp(message, "time", length) == 0)
        return RESPONSE_TIME;
    else if (strncmp(message, "stats", length) == 0)
        return RESPONSE_STATS;
    else if (strncmp(message, "shutdown", length) == 0)
        return RESPONSE_SHUTDOWN;

    return RESPONSE_UNKNOWN;
}
