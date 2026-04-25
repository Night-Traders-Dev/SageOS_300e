#include <string.h>
// Just adding strlen manually to be safe.
size_t my_strlen(const char *s) {
    size_t l = 0;
    while (*s++) l++;
    return l;
}
