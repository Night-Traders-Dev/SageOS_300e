#include <stdint.h>
#include <stddef.h>
#include "console.h"

// Very simple JSON parser for: {"name": "...", "binary": "..."}
// We only support keys "name" and "binary"
void json_parse_command(const char *data, char *name, char *binary) {
    const char *p = data;
    // Basic search for "name": "..."
    // ...
}
