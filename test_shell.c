#include <stdio.h>
#include <string.h>

int main() {
    const char *line = "ls";
    if (strcmp(line, "sageshell") == 0) {
        printf("Match\n");
    } else {
        printf("No Match: %s\n", line);
    }
    return 0;
}
