#include <stdint.h>
#include <stddef.h>
#include "console.h"

// Very simple JSON parser for: {"name": "...", "binary": "..."}
// We only support keys "name" and "binary"
void json_parse_command(const char *data, char *name, char *binary) {
    (void)data;
    (void)name;
    (void)binary;
    // Basic search for "name": "..."
    // ...
}
